#!/usr/bin/env python3
"""
sinand — 司南 Mac 端服务。

只用标准库。装 venv、装依赖、依赖版本冲突这些事一件都不会发生，
放进 launchd 就能一直跑。

职责：
  1. 采集各家 CLI 的额度与运行状态，归一化后推给设备（Fleet）
  2. 每天推一次号码 / 黄历 / 年轮（Almanac）
  3. 收设备上传的音频，转写后等 B 键确认，再交给选中的编程工具

它不碰 BLE。Ward 那条线是设备直连 Claude 桌面端的，跟这里无关。
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import ipaddress
import json
import logging
import os
import queue
import re
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
try:
    import tomllib                      # 3.11+
except ModuleNotFoundError:             # 3.10 及以下
    import tomli as tomllib             # type: ignore
import wave
from datetime import date, datetime
from pathlib import Path

LOG = logging.getLogger("sinand")
HERE = Path(__file__).resolve().parent
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  # RFC 6455 magic

# ---------------------------------------------------------------- 工具


def is_private(addr: str) -> bool:
    """RFC1918 + 回环。用标准库判，别自己切字符串。"""
    try:
        ip = ipaddress.ip_address(addr)
    except ValueError:
        return False
    return ip.is_private or ip.is_loopback or ip.is_link_local


# ---------------------------------------------------------------- 配置


def load_config() -> dict:
    path = HERE / "config.toml"
    if not path.exists():
        shutil.copy(HERE / "config.example.toml", path)
        LOG.info("created %s from example", path)
    with path.open("rb") as f:
        return tomllib.load(f)


def load_or_create_token(cfg: dict) -> str:
    """Load the LAN pairing secret without ever logging it.

    The bridge is intentionally reachable from the local network, so a private
    source address is not authentication.  The first daemon start creates a
    256-bit token with owner-only permissions; the same value is entered once
    in the watch's local provisioning page.
    """
    raw = cfg.get("security", {}).get("token_file", "~/.sinan/bridge.token")
    path = Path(raw).expanduser()
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    if not path.exists():
        token = secrets.token_hex(32)
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(token + "\n")
        except FileExistsError:
            pass
    token = path.read_text(encoding="utf-8").strip()
    if len(token) < 32:
        raise RuntimeError(f"pairing token is missing or too short: {path}")
    try:
        path.chmod(0o600)
    except OSError:
        pass
    return token


# ---------------------------------------------------------------- WebSocket


class WSError(Exception):
    pass


class WSConn:
    """一个极简的 RFC 6455 服务端连接。够用就好，不追求完整。"""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.lock = threading.Lock()
        self.alive = True

    def handshake(self, expected_token: str) -> None:
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WSError("closed during handshake")
            data += chunk
            if len(data) > 16384:
                raise WSError("header too large")

        headers = {}
        for line in data.split(b"\r\n")[1:]:
            if b":" in line:
                k, v = line.split(b":", 1)
                headers[k.strip().lower()] = v.strip()

        key = headers.get(b"sec-websocket-key")
        if not key:
            raise WSError("no ws key")
        supplied = headers.get(b"x-sinan-token", b"").decode("utf-8", "replace")
        if not hmac.compare_digest(supplied, expected_token):
            self.sock.sendall(
                b"HTTP/1.1 401 Unauthorized\r\n"
                b"Connection: close\r\nContent-Length: 0\r\n\r\n"
            )
            raise WSError("unauthorized")
        accept = base64.b64encode(hashlib.sha1(key + GUID.encode()).digest()).decode()
        self.sock.sendall(
            b"HTTP/1.1 101 Switching Protocols\r\n"
            b"Upgrade: websocket\r\n"
            b"Connection: Upgrade\r\n"
            b"Sec-WebSocket-Accept: " + accept.encode() + b"\r\n\r\n"
        )

    def _recv_exact(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise WSError("closed")
            buf += chunk
        return buf

    def recv(self) -> str | None:
        """读一帧文本。控制帧内部消化掉，返回 None 表示对端要走了。"""
        while True:
            head = self._recv_exact(2)
            fin = head[0] & 0x80
            opcode = head[0] & 0x0F
            masked = head[1] & 0x80
            length = head[1] & 0x7F

            if length == 126:
                length = struct.unpack(">H", self._recv_exact(2))[0]
            elif length == 127:
                length = struct.unpack(">Q", self._recv_exact(8))[0]
            if length > 4 * 1024 * 1024:
                raise WSError("frame too large")

            mask = self._recv_exact(4) if masked else b""
            payload = self._recv_exact(length)
            if masked:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

            if opcode == 0x8:
                return None
            if opcode == 0x9:
                self._send_frame(payload, 0xA)
                continue
            if opcode == 0xA:
                continue
            if opcode == 0x1 and fin:
                return payload.decode("utf-8", "replace")
            # 分片文本：设备端不会发，忽略即可
            if opcode == 0x1:
                acc = payload
                while True:
                    h2 = self._recv_exact(2)
                    f2 = h2[0] & 0x80
                    l2 = h2[1] & 0x7F
                    m2 = h2[1] & 0x80
                    if l2 == 126:
                        l2 = struct.unpack(">H", self._recv_exact(2))[0]
                    elif l2 == 127:
                        l2 = struct.unpack(">Q", self._recv_exact(8))[0]
                    mk = self._recv_exact(4) if m2 else b""
                    p2 = self._recv_exact(l2)
                    if m2:
                        p2 = bytes(b ^ mk[i % 4] for i, b in enumerate(p2))
                    acc += p2
                    if f2:
                        break
                return acc.decode("utf-8", "replace")

    def _send_frame(self, payload: bytes, opcode: int = 0x1) -> None:
        n = len(payload)
        if n < 126:
            head = struct.pack(">BB", 0x80 | opcode, n)
        elif n < 65536:
            head = struct.pack(">BBH", 0x80 | opcode, 126, n)
        else:
            head = struct.pack(">BBQ", 0x80 | opcode, 127, n)
        with self.lock:
            self.sock.sendall(head + payload)

    def send_json(self, obj: dict) -> None:
        try:
            self._send_frame(json.dumps(obj, ensure_ascii=False).encode())
        except OSError:
            self.alive = False

    def close(self) -> None:
        self.alive = False
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------- 数据源


class Fleet:
    """把各家 CLI 的额度口径归一化成 0.0–1.0。

    设备端不做业务判断 —— 它只负责把弧画短，判断在这里做完。
    """

    def __init__(self, cfg: dict):
        self.cfg = cfg

    @staticmethod
    def _process_alive(pattern: str) -> bool:
        if not pattern:
            return False
        try:
            result = subprocess.run(
                ["/usr/bin/pgrep", "-f", pattern], capture_output=True,
                text=True, timeout=3,
            )
            return result.returncode == 0 and bool(result.stdout.strip())
        except (OSError, subprocess.SubprocessError):
            return False

    def _builtin_probe(self, w: dict) -> dict | None:
        kind = w.get("kind")
        if kind == "cli":
            command = str(w.get("command", ""))
            path = self.cfg["daemon"].get("path", os.environ.get("PATH", ""))
            installed = bool(shutil.which(command, path=path))
            active = installed and self._process_alive(str(w.get("active_pattern", "")))
            return {
                "state": "run" if active else ("idle" if installed else "down"),
                "quota": 1.0 if installed else 0.0,
                "task": w.get("running_task", "voice task") if active else
                        (w.get("online_task", "ready") if installed else ""),
            }
        if kind == "process":
            alive = self._process_alive(str(w.get("pattern", "")))
            return {
                "state": "idle" if alive else "down",
                "quota": 1.0 if alive else 0.0,
                "task": w.get("online_task", "online") if alive else "",
            }
        if kind == "paseo":
            path = self.cfg["daemon"].get("path", os.environ.get("PATH", ""))
            installed = bool(shutil.which("paseo", path=path))
            root = Path(w.get("agents", "~/.paseo/agents")).expanduser()
            files = list(root.glob("**/*.json")) if root.exists() else []
            if not files:
                return {"state": "idle" if installed else "down",
                        "quota": 1.0 if installed else 0.0,
                        "task": "ready" if installed else ""}
            latest = max(files, key=lambda p: p.stat().st_mtime)
            try:
                data = json.loads(latest.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                return {"state": "stall", "quota": 0.0, "task": "state unreadable"}
            age = time.time() - latest.stat().st_mtime
            status = str(data.get("lastStatus", "idle")).lower()
            reason = str(data.get("attentionReason", ""))
            if status in ("running", "working", "busy"):
                state = "run"
            elif data.get("requiresAttention") and reason not in ("", "finished"):
                state = "stall"
            elif self._process_alive("Paseo (Supervisor|Daemon)|@getpaseo"):
                state = "idle"
            else:
                # CLI 已安装时 Work 可以按需拉起 daemon；不把“当前没进程”误报成不可用。
                state = "idle" if installed else "down"
            title = str(data.get("title", ""))[:48] if age < 86400 and state in ("run", "stall") else ""
            if state == "idle" and installed:
                title = "ready"
            return {"state": state, "quota": 1.0 if state != "down" else 0.0, "task": title}
        if kind == "knowledge_base":
            path = Path(w.get("status_file", "")).expanduser()
            if not path.exists():
                return {"state": "down", "quota": 0.0, "task": "status missing"}
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                layers = data.get("combined_layers", {})
                layer = str(data.get("default_search_layer", "ready"))
                chunks = int(layers.get(f"{layer}_chunks", 0))
                warnings = data.get("warnings", [])
                state = "stall" if warnings else "idle"
                task = f"{layer} {chunks} chunks" if chunks else layer
                return {"state": state, "quota": 1.0, "task": task}
            except (OSError, ValueError, json.JSONDecodeError):
                return {"state": "stall", "quota": 0.0, "task": "status unreadable"}
        return None

    def _probe(self, w: dict) -> dict:
        state, quota, task = "down", 0.0, ""
        builtin = self._builtin_probe(w)
        if builtin is not None:
            state = builtin["state"]
            quota = builtin["quota"]
            task = builtin["task"]
        probe = w.get("probe")
        if probe and builtin is None:
            try:
                out = subprocess.run(
                    ["/bin/bash", "--norc", "--noprofile", "-c", probe],
                    capture_output=True, text=True, timeout=w.get("timeout", 8),
                    env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
                ).stdout.strip()
                # 约定：探针输出一行 JSON，字段 state / quota / task
                got = json.loads(out) if out.startswith("{") else {}
                state = got.get("state", "idle")
                quota = float(got.get("quota", 1.0))
                task = got.get("task", "")
            except Exception as e:  # 探针挂了就是 down，不要让它拖垮整个推送
                LOG.debug("probe failed for %s: %s", w.get("id"), e)
        return {
            "id": w.get("id", "?"),
            "label": w.get("label", w.get("id", "?"))[:8],
            "state": state,
            "quota": max(0.0, min(1.0, quota)),
            "task": task[:48],
        }

    def snapshot(self) -> dict:
        workers = [self._probe(w) for w in self.cfg.get("worker", [])]
        return {"t": "fleet", "ts": int(time.time()), "workers": workers}


class Almanac:
    def __init__(self, cfg: dict):
        self.cfg = cfg

    def snapshot(self) -> dict:
        doy = date.today().timetuple().tm_yday
        out = {
            "t": "almanac",
            "number": {"code": "", "title": ""},
            "huangli": {"trend": "flat", "yi": "", "ji": ""},
            "ring": [{"doy": doy, "tag": ""}],
        }
        almanac_cfg = self.cfg.get("almanac", {})
        archive = Path(almanac_cfg.get(
            "number_archive", "~/.local/share/the-machine/archive/numbers"
        )).expanduser()
        today_file = archive / f"{date.today().isoformat()}.md"
        if today_file.exists():
            try:
                text = today_file.read_text(encoding="utf-8")
                title = re.search(r"^## 号码：(.+)$", text, re.MULTILINE)
                grade = re.search(r"^- \*\*等级：(.+?)\*\*$", text, re.MULTILINE)
                summary = re.search(r"^- \*\*一句话：\*\*\s*(.+)$", text, re.MULTILINE)
                out["number"] = {
                    "code": grade.group(1)[:16] if grade else "",
                    "title": title.group(1).strip()[:48] if title else "",
                }
                if summary:
                    out["huangli"]["yi"] = summary.group(1).strip()[:48]
                out["ring"] = [{"doy": doy, "tag": "daily number"}]
            except OSError as exc:
                LOG.debug("number archive read failed: %s", exc)

        src = almanac_cfg.get("source")
        if not src:
            return out
        try:
            raw = subprocess.run(
                ["/bin/bash", "--norc", "--noprofile", "-c", src],
                capture_output=True, text=True, timeout=30,
                env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
            ).stdout.strip()
            got = json.loads(raw)
            for k in ("number", "huangli", "ring"):
                if k in got:
                    out[k] = got[k]
        except Exception as e:
            LOG.warning("almanac source failed: %s", e)
        return out


class CodexAppServer:
    """A persistent Codex thread over the official local Unix-socket server."""

    def __init__(self, tool: str, cfg: dict, cwd: Path, env: dict[str, str]):
        self.tool = tool
        self.cfg = cfg
        self.cwd = cwd
        self.env = env
        self.sock = None
        self.thread_id = ""
        self.next_id = 1
        self.lock = threading.Lock()
        self.messages = queue.Queue()

    @staticmethod
    def _exact(sock: socket.socket, count: int) -> bytes:
        data = bytearray()
        while len(data) < count:
            part = sock.recv(count - len(data))
            if not part:
                raise RuntimeError("Codex app-server socket closed")
            data.extend(part)
        return bytes(data)

    @staticmethod
    def _frame(payload: bytes, opcode: int = 1) -> bytes:
        first = 0x80 | opcode
        mask = os.urandom(4)
        length = len(payload)
        if length < 126:
            header = bytes([first, 0x80 | length])
        elif length < 65536:
            header = bytes([first, 0x80 | 126]) + struct.pack("!H", length)
        else:
            header = bytes([first, 0x80 | 127]) + struct.pack("!Q", length)
        masked = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
        return header + mask + masked

    def _send(self, obj: dict) -> None:
        if not self.sock:
            raise RuntimeError("Codex app-server is not connected")
        payload = json.dumps(obj, ensure_ascii=False).encode()
        self.sock.sendall(self._frame(payload))

    def _recv_ws(self, sock: socket.socket) -> str:
        chunks = []
        while True:
            first, second = self._exact(sock, 2)
            opcode = first & 0x0F
            length = second & 0x7F
            if length == 126:
                length = struct.unpack("!H", self._exact(sock, 2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self._exact(sock, 8))[0]
            masked = bool(second & 0x80)
            mask = self._exact(sock, 4) if masked else b""
            payload = self._exact(sock, length)
            if masked:
                payload = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
            if opcode == 8:
                raise RuntimeError("Codex app-server closed the WebSocket")
            if opcode == 9:
                sock.sendall(self._frame(payload, opcode=10))
                continue
            if opcode in (1, 0):
                chunks.append(payload)
                if first & 0x80:
                    return b"".join(chunks).decode()

    def _read_messages(self, sock: socket.socket, messages: queue.Queue) -> None:
        try:
            while True:
                messages.put(json.loads(self._recv_ws(sock)))
        except Exception as e:
            messages.put({"_stream_error": f"Codex response stream failed: {e}"})

    def _recv(self, deadline: float, cancelled=None) -> dict:
        if not self.sock:
            raise RuntimeError("Codex app-server is not connected")
        while True:
            if cancelled and cancelled():
                raise RuntimeError("device disconnected; task cancelled")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("Codex app-server response timed out")
            try:
                msg = self.messages.get(timeout=min(1.0, remaining))
            except queue.Empty:
                continue
            if "_stream_error" in msg:
                raise RuntimeError(msg["_stream_error"])
            return msg

    def _request(self, method: str, params: dict, timeout: int = 30,
                 cancelled=None) -> dict:
        request_id = self.next_id
        self.next_id += 1
        self._send({"id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + timeout
        while True:
            msg = self._recv(deadline, cancelled)
            if msg.get("id") != request_id:
                continue
            if "error" in msg:
                raise RuntimeError(str(msg["error"])[:300])
            return msg.get("result", {})

    def _connect(self) -> socket.socket:
        socket_path = Path(self.cfg.get(
            "codex_socket", "~/.codex/app-server-control/app-server-control.sock"
        )).expanduser()
        if not socket_path.exists():
            started = subprocess.run(
                [self.tool, "app-server", "daemon", "start"],
                capture_output=True, text=True, timeout=30, env=self.env,
            )
            if started.returncode != 0:
                raise RuntimeError("Codex app-server daemon could not start")
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(10)
        sock.connect(str(socket_path))
        key = base64.b64encode(os.urandom(16)).decode()
        request = (
            "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode()
        sock.sendall(request)
        response = bytearray()
        while b"\r\n\r\n" not in response and len(response) < 8192:
            response.extend(sock.recv(1024))
        if b" 101 " not in response.split(b"\r\n", 1)[0]:
            sock.close()
            raise RuntimeError("Codex app-server WebSocket handshake failed")
        sock.settimeout(None)
        return sock

    def _start(self) -> None:
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = self._connect()
        self.thread_id = ""
        self.messages = queue.Queue()
        threading.Thread(
            target=self._read_messages, args=(self.sock, self.messages), daemon=True,
        ).start()
        self.next_id = 1
        LOG.info("connecting to persistent Codex app-server")
        self._request("initialize", {
            "clientInfo": {"name": "sinan", "title": "SINAN Work", "version": "3.1.1"},
            "capabilities": {"experimentalApi": True},
        }, timeout=30)
        self._send({"method": "initialized", "params": {}})
        started = self._request("thread/start", {
            "cwd": str(self.cwd),
            "model": self.cfg.get("codex_model", "gpt-5.6-terra"),
            "approvalPolicy": "never", "sandbox": "workspace-write",
            "ephemeral": True, "baseInstructions": ProgrammingRouter.SAFETY,
        }, timeout=30)
        self.thread_id = started["thread"]["id"]
        LOG.info("persistent Codex session ready")

    def _ensure_started(self) -> None:
        if not self.sock or not self.thread_id:
            self._start()

    def warm(self) -> None:
        with self.lock:
            self._ensure_started()

    def _interrupt(self, turn_id: str) -> None:
        if not turn_id or not self.thread_id:
            return
        request_id = self.next_id
        self.next_id += 1
        try:
            self._send({"id": request_id, "method": "turn/interrupt", "params": {
                "threadId": self.thread_id, "turnId": turn_id,
            }})
        except Exception:
            pass

    def run(self, text: str, timeout: int, cancelled=None) -> str:
        with self.lock:
            self._ensure_started()
            started = self._request("turn/start", {
                "threadId": self.thread_id,
                "effort": self.cfg.get("codex_reasoning", "low"),
                "approvalPolicy": "never",
                "input": [{"type": "text", "text": text}],
            }, timeout=30, cancelled=cancelled)
            turn_id = started["turn"]["id"]
            answer = ""
            deadline = time.monotonic() + timeout
            try:
                while True:
                    msg = self._recv(deadline, cancelled)
                    if msg.get("method") == "item/completed":
                        params = msg.get("params", {})
                        if params.get("turnId") != turn_id:
                            continue
                        item = params.get("item", {})
                        if item.get("type") == "agentMessage" and item.get("text"):
                            answer = item["text"]
                    elif msg.get("method") == "turn/completed":
                        params = msg.get("params", {})
                        if params.get("turn", {}).get("id") == turn_id:
                            break
                    elif "id" in msg and msg.get("method"):
                        self._send({"id": msg["id"], "error": {
                            "code": -32001, "message": "approvals disabled for watch session",
                        }})
            except Exception:
                self._interrupt(turn_id)
                raise
            return answer


class ProgrammingRouter:
    """Only three named coding tools; spoken text is always a separate argv."""

    TARGETS = {"codex", "paseo", "grok"}
    SAFETY = (
        "这是用户从 M5StopWatch Work 发出的语音编程指令。"
        "严格处理这条指令；禁止删除任何本地文件、文件夹、消息、数据库、"
        "缓存、模型或备份。若需要未获得的外部权限，清楚说明阻塞。\n\n"
    )

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.codex_session = None

    def warm_codex(self) -> None:
        cfg = self.cfg.get("programming", {})
        if cfg.get("codex_transport", "app-server") != "app-server":
            return
        try:
            cwd = Path(cfg.get("cwd", str(Path.home()))).expanduser().resolve()
            session = CodexAppServer(self._tool("codex"), cfg, cwd, self._env())
            self.codex_session = session
            session.warm()
        except Exception as e:
            LOG.warning("Codex app-server warmup failed: %s", e)

    def _env(self) -> dict[str, str]:
        daemon = self.cfg.get("daemon", {})
        env = {
            "PATH": daemon.get("path", os.environ.get("PATH", "")),
            "HOME": str(Path.home()),
            "LANG": os.environ.get("LANG", "zh_CN.UTF-8"),
        }
        if os.environ.get("TMPDIR"):
            env["TMPDIR"] = os.environ["TMPDIR"]
        return env

    def _tool(self, name: str) -> str:
        configured = self.cfg.get("programming", {}).get(f"{name}_cmd", name)
        expanded = str(Path(configured).expanduser()) if "/" in configured else configured
        found = shutil.which(expanded, path=self._env()["PATH"])
        if not found:
            raise RuntimeError(f"{name} is not installed")
        return found

    @staticmethod
    def _extract_json_text(value) -> str:
        if isinstance(value, dict):
            for key in ("finalMessage", "final_message", "response", "output", "result", "text", "summary"):
                got = value.get(key)
                if isinstance(got, str) and got.strip():
                    return got.strip()
            for got in value.values():
                text = ProgrammingRouter._extract_json_text(got)
                if text:
                    return text
        elif isinstance(value, list):
            for got in reversed(value):
                text = ProgrammingRouter._extract_json_text(got)
                if text:
                    return text
        return ""

    @staticmethod
    def _display(raw: str) -> str:
        clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", raw).strip()
        try:
            extracted = ProgrammingRouter._extract_json_text(json.loads(clean))
            if extracted:
                clean = extracted
        except json.JSONDecodeError:
            pass
        clean = re.sub(r"\n{3,}", "\n\n", clean)
        return clean[-900:] if clean else "已完成，没有文字回复"

    @staticmethod
    def _paseo_agent_id(raw: str) -> str:
        match = re.search(r'"agentId"\s*:\s*"([^"]+)"', raw)
        return match.group(1) if match else ""

    @staticmethod
    def _paseo_text(raw: str) -> str:
        """PASEO text logs mark user/thought events; assistant text is plain."""
        answer = []
        for line in raw.splitlines():
            stripped = line.strip()
            if not stripped or stripped.startswith("["):
                continue
            answer.append(stripped)
        return "\n".join(answer).strip()

    def _ensure_paseo(self, paseo: str, env: dict[str, str]) -> None:
        status = subprocess.run(
            [paseo, "daemon", "status", "--json"], capture_output=True,
            text=True, timeout=8, env=env,
        )
        if status.returncode == 0 and "DAEMON_NOT_RUNNING" not in status.stdout:
            return
        started = subprocess.run(
            [paseo, "daemon", "start", "--no-relay", "--no-web-ui"],
            capture_output=True, text=True, timeout=20, env=env,
        )
        if started.returncode != 0:
            raise RuntimeError("PASEO daemon could not start")

    @staticmethod
    def _run_process(argv: list[str], cwd: Path, env: dict[str, str],
                     timeout: int, cancelled=None) -> subprocess.CompletedProcess:
        """Run one CLI while allowing a disconnected watch to cancel it."""
        proc = subprocess.Popen(
            argv, cwd=str(cwd), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=env,
        )
        deadline = time.monotonic() + timeout
        while True:
            if cancelled and cancelled():
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
                raise RuntimeError("device disconnected; task cancelled")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
                raise RuntimeError(f"programming task timed out after {timeout}s")
            try:
                stdout, stderr = proc.communicate(timeout=min(1.0, remaining))
                return subprocess.CompletedProcess(argv, proc.returncode, stdout, stderr)
            except subprocess.TimeoutExpired:
                continue

    def run(self, target: str, text: str, cancelled=None) -> str:
        if target not in self.TARGETS:
            raise ValueError("target is not allowed")
        spoken = text.strip()
        if not spoken:
            raise ValueError("empty instruction")

        cfg = self.cfg.get("programming", {})
        cwd = Path(cfg.get("cwd", str(Path.home()))).expanduser().resolve()
        if not cwd.is_dir():
            raise RuntimeError("programming cwd is missing")
        timeout = int(cfg.get(f"{target}_timeout", cfg.get("timeout", 600)))
        prompt = self.SAFETY + spoken
        env = self._env()

        if target == "codex":
            tool = self._tool("codex")
            if cfg.get("codex_transport", "app-server") == "app-server":
                if self.codex_session is None:
                    self.codex_session = CodexAppServer(tool, cfg, cwd, env)
                return self._display(self.codex_session.run(spoken, timeout, cancelled))
            argv = [tool, "-m", cfg.get("codex_model", "gpt-5.6-terra"),
                    "-c", f'model_reasoning_effort="{cfg.get("codex_reasoning", "low")}"',
                    "-a", "never", "-s", "workspace-write", "exec", "-C", str(cwd),
                    "--skip-git-repo-check", "--ephemeral", "--ignore-user-config",
                    "--color", "never", prompt]
        elif target == "paseo":
            tool = self._tool("paseo")
            self._ensure_paseo(tool, env)
            argv = [tool, "run", "--provider",
                    cfg.get("paseo_provider", "claude"),
                    "--cwd", str(cwd), "--mode",
                    cfg.get("paseo_mode", "default"),
                    "--wait-timeout", f"{timeout}s", "--json", prompt]
        else:
            tool = self._tool("grok")
            argv = [tool, "--cwd", str(cwd), "--output-format", "plain",
                    "--permission-mode", cfg.get("grok_mode", "default"),
                    "--no-subagents", "-p", prompt]

        result = self._run_process(argv, cwd, env, timeout, cancelled)
        raw = result.stdout.strip() or result.stderr.strip()
        if result.returncode != 0:
            raise RuntimeError(self._display(raw)[:300] or f"{target} failed")
        if target == "paseo":
            agent_id = self._paseo_agent_id(raw)
            if agent_id:
                logs = subprocess.run(
                    [tool, "agent", "logs", agent_id, "--filter", "text", "--tail", "50"],
                    cwd=str(cwd), capture_output=True, text=True,
                    timeout=20, env=env,
                )
                reply = self._paseo_text(logs.stdout)
                if logs.returncode == 0 and reply:
                    return self._display(reply)
        return self._display(raw)


# ---------------------------------------------------------------- 语音


class VoiceSession:
    """一次录音的生命周期。攒够整段再转写，比流式简单得多，
    而且十来秒的语音也没必要流式。"""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.pcm = bytearray()
        self.rate = 16000
        self.target = ""

    def begin(self, rate: int, target: str) -> None:
        self.pcm = bytearray()
        self.rate = rate or 16000
        self.target = target if target in ProgrammingRouter.TARGETS else ""

    def chunk(self, b64: str) -> None:
        try:
            self.pcm += base64.b64decode(b64)
        except Exception:
            pass

    def _write_wav(self) -> Path:
        path = Path("/tmp/sinan_asr.wav")
        with wave.open(str(path), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(self.rate)
            w.writeframes(bytes(self.pcm))
        return path

    def transcribe(self) -> str:
        if len(self.pcm) < 4000:
            return ""
        wav = self._write_wav()
        cmd = self.cfg["voice"]["asr_cmd"].replace("{wav}", str(wav))
        try:
            output = subprocess.run(
                ["/bin/bash", "--norc", "--noprofile", "-c", cmd],
                capture_output=True, text=True, timeout=90,
                env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
            ).stdout
            # whisper-cli may split a longer instruction into several result lines.
            # Work sends one programming instruction, so preserve every segment.
            return re.sub(r"\s+", " ", output).strip()
        except Exception as e:
            LOG.warning("asr failed: %s", e)
            return ""

    def synth(self, text: str) -> str:
        """macOS 自带 say 就够用，不用装 TTS。转 16k 单声道 s16le 回传。

        走 WAV 而不是 CAF：CAF 的头不是定长，早期版本硬跳 4096 字节，
        运气不好就把音频前半秒切掉、或者把头当成采样播出一声爆音。
        """
        if not text:
            return ""
        aiff = Path("/tmp/sinan_tts.aiff")
        wav = Path("/tmp/sinan_tts.wav")
        voice = self.cfg["voice"].get("tts_voice", "Tingting")
        try:
            subprocess.run(["say", "-v", voice, "-o", str(aiff), text],
                           check=True, timeout=60)
            subprocess.run(
                ["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1",
                 str(aiff), str(wav)], check=True, timeout=60)
            with wave.open(str(wav), "rb") as w:
                if w.getnchannels() != 1 or w.getsampwidth() != 2:
                    LOG.warning("unexpected tts format, skipping audio")
                    return ""
                pcm = w.readframes(w.getnframes())
            return base64.b64encode(pcm).decode()
        except Exception as e:
            LOG.warning("tts failed: %s", e)
            return ""


# ---------------------------------------------------------------- 主服务


class Server:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.token = load_or_create_token(cfg)
        self.fleet = Fleet(cfg)
        self.almanac = Almanac(cfg)
        self.programming = ProgrammingRouter(cfg)
        self.clients: list[WSConn] = []
        self.clients_lock = threading.Lock()

    def broadcast(self, obj: dict) -> None:
        with self.clients_lock:
            targets = list(self.clients)
        for c in targets:
            if c.alive:
                c.send_json(obj)

    def pusher(self) -> None:
        """周期推送。Fleet 勤一点，Almanac 一天几次就够。"""
        last_almanac = 0.0
        while True:
            try:
                self.broadcast(self.fleet.snapshot())
                if time.time() - last_almanac > self.cfg["daemon"].get("almanac_interval", 3600):
                    last_almanac = time.time()
                    self.broadcast(self.almanac.snapshot())
            except Exception as e:
                LOG.warning("pusher: %s", e)
            time.sleep(self.cfg["daemon"].get("fleet_interval", 20))

    def run_action(self, action_id: str) -> str:
        if action_id == "daily_number":
            number = self.almanac.snapshot().get("number", {})
            return str(number.get("title") or number.get("code") or "今天还没有号码")[:400]
        return "这个动作没有开放"

    def handle(self, conn: WSConn) -> None:
        session = VoiceSession(self.cfg)
        pending = {"target": "", "text": ""}
        pending_lock = threading.Lock()
        with self.clients_lock:
            self.clients.append(conn)
        LOG.info("device connected (%d total)", len(self.clients))

        # 一连上先给一份现状，别让设备干等一个推送周期
        conn.send_json(self.fleet.snapshot())
        conn.send_json(self.almanac.snapshot())

        try:
            while conn.alive:
                line = conn.recv()
                if line is None:
                    break
                try:
                    msg = json.loads(line)
                except json.JSONDecodeError:
                    continue

                t = msg.get("t")
                if t == "hello":
                    LOG.info("hello from %s v%s", msg.get("dev"), msg.get("ver"))
                elif t == "asr_begin":
                    session.begin(msg.get("rate", 16000), str(msg.get("target", "")))
                elif t == "asr_chunk":
                    session.chunk(msg.get("pcm", ""))
                elif t == "asr_end":
                    threading.Thread(target=self._finish_transcription,
                                     args=(conn, session, pending, pending_lock),
                                     daemon=True).start()
                    session = VoiceSession(self.cfg)
                elif t == "asr_cancel":
                    session = VoiceSession(self.cfg)
                    with pending_lock:
                        pending["target"] = ""
                        pending["text"] = ""
                elif t == "voice_send":
                    threading.Thread(target=self._run_programming,
                                     args=(conn, str(msg.get("target", "")),
                                           pending, pending_lock),
                                     daemon=True).start()
                elif t == "act":
                    out = self.run_action(msg.get("id", ""))
                    conn.send_json({"t": "say", "text": out, "pcm": ""})
                elif t == "pong":
                    pass
        except (WSError, OSError) as e:
            LOG.debug("conn ended: %s", e)
        finally:
            conn.close()
            with self.clients_lock:
                if conn in self.clients:
                    self.clients.remove(conn)
            LOG.info("device disconnected")

    def _finish_transcription(self, conn: WSConn, session: VoiceSession,
                              pending: dict, pending_lock: threading.Lock) -> None:
        """A 停止后只转写；B 键消息到达前绝不运行任何编程工具。"""
        if not session.target:
            conn.send_json({"t": "voice_status", "phase": "error", "note": "unknown target"})
            return
        conn.send_json({"t": "voice_status", "phase": "transcribing"})
        heard = session.transcribe()
        if not heard:
            conn.send_json({"t": "asr_result", "text": ""})
            return
        with pending_lock:
            pending["target"] = session.target
            pending["text"] = heard
        conn.send_json({"t": "asr_result", "text": heard})
        LOG.info("voice transcript ready for %s (%d chars)", session.target, len(heard))
        conn.send_json({"t": "voice_status", "phase": "ready"})

    def _run_programming(self, conn: WSConn, target: str,
                         pending: dict, pending_lock: threading.Lock) -> None:
        """Consume exactly one confirmed transcript and run its selected tool."""
        with pending_lock:
            if target not in ProgrammingRouter.TARGETS or pending.get("target") != target:
                conn.send_json({"t": "voice_status", "phase": "error", "note": "nothing to send"})
                return
            heard = str(pending.get("text", ""))
            pending["target"] = ""
            pending["text"] = ""
        conn.send_json({"t": "voice_status", "phase": "running"})
        try:
            reply = self.programming.run(target, heard, lambda: not conn.alive)
        except Exception as e:
            LOG.warning("%s failed: %s", target, e)
            note = str(e).strip()[:180] or f"{target} failed"
            conn.send_json({"t": "voice_status", "phase": "error", "note": note})
            return
        # 编程回复往往很长，不让手表朗读几分钟；屏幕显示精简后的尾部结果。
        conn.send_json({"t": "say", "text": reply, "pcm": ""})

    def serve(self) -> None:
        host = self.cfg["daemon"].get("bind", "0.0.0.0")
        port = self.cfg["daemon"].get("port", 8790)

        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(4)
        LOG.info("listening on %s:%d", host, port)

        threading.Thread(target=self.pusher, daemon=True).start()
        threading.Thread(target=self.programming.warm_codex, daemon=True).start()

        while True:
            sock, addr = srv.accept()
            # 只接内网。这台机器上跑着你的 CLI，不该对公网开口。
            # 注意 172 段：私网只有 172.16-172.31，172.32 是公网地址，
            # 用 startswith("172.") 会把公网一起放进来
            if not is_private(addr[0]):
                LOG.warning("rejected %s", addr[0])
                sock.close()
                continue
            conn = WSConn(sock)
            try:
                conn.handshake(self.token)
            except (WSError, OSError):
                conn.close()
                continue
            threading.Thread(target=self.handle, args=(conn,), daemon=True).start()


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)-5s %(message)s",
        datefmt="%H:%M:%S",
    )
    cfg = load_config()
    Server(cfg).serve()
    return 0


if __name__ == "__main__":
    sys.exit(main())

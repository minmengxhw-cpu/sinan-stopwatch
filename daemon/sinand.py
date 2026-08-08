#!/usr/bin/env python3
"""
sinand — 司南 Mac 端服务。

只用标准库。装 venv、装依赖、依赖版本冲突这些事一件都不会发生，
放进 launchd 就能一直跑。

职责：
  1. 采集各家 CLI 的额度与运行状态，归一化后推给设备（Fleet）
  2. 每天推一次号码 / 黄历 / 年轮（Almanac）
  3. 收设备上传的音频，转写，交给白名单动作，把结果 TTS 回去（Echo）

它不碰 BLE。Ward 那条线是设备直连 Claude 桌面端的，跟这里无关。
"""

from __future__ import annotations

import base64
import hashlib
import hmac
import json
import logging
import os
import shlex
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import tomllib
import wave
from datetime import date, datetime
from pathlib import Path

LOG = logging.getLogger("sinand")
HERE = Path(__file__).resolve().parent
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"  # RFC 6455 magic

# ---------------------------------------------------------------- 配置


def load_config() -> dict:
    path = HERE / "config.toml"
    if not path.exists():
        shutil.copy(HERE / "config.example.toml", path)
        LOG.info("created %s from example", path)
    with path.open("rb") as f:
        return tomllib.load(f)


# ---------------------------------------------------------------- WebSocket


class WSError(Exception):
    pass


class WSConn:
    """一个极简的 RFC 6455 服务端连接。够用就好，不追求完整。"""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.lock = threading.Lock()
        self.alive = True
        self.headers: dict[bytes, bytes] = {}

    def handshake(self) -> None:
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
        self.headers = headers

        key = headers.get(b"sec-websocket-key")
        if not key:
            raise WSError("no ws key")
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

    def _probe(self, w: dict) -> dict:
        state, quota, task = "down", 0.0, ""
        probe = w.get("probe")
        if probe:
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
        src = self.cfg.get("almanac", {}).get("source")
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


# ---------------------------------------------------------------- 语音


class VoiceSession:
    """一次录音的生命周期。攒够整段再转写，比流式简单得多，
    而且十来秒的语音也没必要流式。"""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.pcm = bytearray()
        self.rate = 16000

    def begin(self, rate: int) -> None:
        self.pcm = bytearray()
        self.rate = rate or 16000

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
        cmd = self.cfg["voice"]["asr_cmd"].replace("{wav}", shlex.quote(str(wav)))
        try:
            return subprocess.run(
                ["/bin/bash", "--norc", "--noprofile", "-c", cmd],
                capture_output=True, text=True, timeout=90,
                env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
            ).stdout.strip()
        except Exception as e:
            LOG.warning("asr failed: %s", e)
            return ""

    def answer(self, text: str) -> str:
        # 用 shlex.quote 而不是 json.dumps：后者产出的双引号字符串在 bash -c 里
        # 仍会展开 $()/反引号命令替换，等于把麦克风收到的任意文本变成命令执行；
        # shlex.quote 产出单引号包裹字符串，bash 单引号内不做任何展开，才是真正安全的转义
        cmd = self.cfg["voice"]["agent_cmd"].replace("{text}", shlex.quote(text))
        try:
            return subprocess.run(
                ["/bin/bash", "--norc", "--noprofile", "-c", cmd],
                capture_output=True, text=True,
                timeout=self.cfg["voice"].get("agent_timeout", 180),
                env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
            ).stdout.strip()[:400]
        except Exception as e:
            LOG.warning("agent failed: %s", e)
            return "agent timed out"

    def synth(self, text: str) -> str:
        """macOS 自带 say 就够用，不用装 TTS。转 16k 单声道 s16le 回传。"""
        if not text:
            return ""
        aiff = Path("/tmp/sinan_tts.aiff")
        raw = Path("/tmp/sinan_tts.raw")
        voice = self.cfg["voice"].get("tts_voice", "Tingting")
        try:
            subprocess.run(["say", "-v", voice, "-o", str(aiff), text],
                           check=True, timeout=60)
            subprocess.run(
                ["afconvert", "-f", "caff", "-d", "LEI16@16000", "-c", "1",
                 str(aiff), str(raw)], check=True, timeout=60)
            data = raw.read_bytes()
            # 跳过 caff 头。用 wave 转一道更稳，但这样少一个临时文件
            return base64.b64encode(data[4096:]).decode()
        except Exception as e:
            LOG.warning("tts failed: %s", e)
            return ""


# ---------------------------------------------------------------- 主服务


class Server:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.fleet = Fleet(cfg)
        self.almanac = Almanac(cfg)
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
        for a in self.cfg.get("action", []):
            if a.get("id") == action_id:
                try:
                    out = subprocess.run(
                        ["/bin/bash", "--norc", "--noprofile", "-c", a["cmd"]],
                        capture_output=True, text=True, timeout=a.get("timeout", 120),
                        env={"PATH": self.cfg["daemon"].get("path", os.environ.get("PATH", ""))},
                    ).stdout.strip()[:400]
                    # 动作完成全屏一闪，设备端 45s 去重
                    self.broadcast({"t": "event", "kind": "done", "id": action_id,
                                    "title": "DONE", "body": action_id})
                    return out
                except Exception as e:
                    self.broadcast({"t": "event", "kind": "error", "id": action_id,
                                    "title": "ERROR", "body": action_id})
                    return f"failed: {e}"
        return f"unknown action: {action_id}"

    def handle(self, conn: WSConn) -> None:
        session = VoiceSession(self.cfg)
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
                    session.begin(msg.get("rate", 16000))
                elif t == "asr_chunk":
                    session.chunk(msg.get("pcm", ""))
                elif t == "asr_end":
                    threading.Thread(target=self._finish_voice,
                                     args=(conn, session), daemon=True).start()
                    session = VoiceSession(self.cfg)
                elif t == "asr_cancel":
                    # 设备端中途取消：丢弃这段缓冲，不转写不上传
                    session = VoiceSession(self.cfg)
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

    def _finish_voice(self, conn: WSConn, session: VoiceSession) -> None:
        heard = session.transcribe()
        conn.send_json({"t": "asr_result", "text": heard})
        if not heard:
            conn.send_json({"t": "say", "text": "didn't catch that", "pcm": ""})
            return
        LOG.info("heard: %s", heard)

        # inject 模式（haosuo 路线）：ASR 结果直接打进当前焦点窗口，
        # 不过 agent_cmd。写代码时说一句话就进了编辑器，这才是对讲
        if self.cfg["voice"].get("inject"):
            try:
                subprocess.run(
                    ["osascript", "-e",
                     'tell application "System Events" to keystroke ' + json.dumps(heard)],
                    capture_output=True, timeout=15)
                self.broadcast({"t": "event", "kind": "done", "id": "voice",
                                "title": "INJECTED", "body": heard[:60]})
                conn.send_json({"t": "say", "text": heard, "pcm": ""})
            except Exception as e:
                LOG.warning("inject failed: %s", e)
                self.broadcast({"t": "event", "kind": "error", "id": "voice",
                                "title": "ERROR", "body": "inject failed"})
                conn.send_json({"t": "say", "text": "inject failed", "pcm": ""})
            return

        reply = session.answer(heard)
        self.broadcast({"t": "event", "kind": "done", "id": "voice",
                        "title": "DONE", "body": heard[:60]})
        conn.send_json({"t": "say", "text": reply, "pcm": session.synth(reply)})

    @staticmethod
    def _is_private_ip(ip: str) -> bool:
        """RFC1918 私网判断。原来的 startswith("172.") 会把 172.0.0.1/172.200.x.x
        这类公网地址也当成内网放行——172.16.0.0/12 的私网范围仅是第二段 16-31。"""
        if ip.startswith(("10.", "192.168.", "127.")):
            return True
        if ip.startswith("172."):
            parts = ip.split(".")
            if len(parts) >= 2 and parts[1].isdigit():
                return 16 <= int(parts[1]) <= 31
            return False
        return False

    def serve(self) -> None:
        host = self.cfg["daemon"].get("bind", "0.0.0.0")
        port = self.cfg["daemon"].get("port", 8790)

        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((host, port))
        srv.listen(4)
        LOG.info("listening on %s:%d", host, port)

        threading.Thread(target=self.pusher, daemon=True).start()

        token = self.cfg["daemon"].get("token", "")

        while True:
            sock, addr = srv.accept()
            # 只接内网。这台机器上跑着你的 CLI，不该对公网开口
            if not self._is_private_ip(addr[0]):
                LOG.warning("rejected %s (not private)", addr[0])
                sock.close()
                continue
            conn = WSConn(sock)
            try:
                conn.handshake()
            except (WSError, OSError):
                conn.close()
                continue
            # 同网段不等于可信：没配 token 就只能防外网，防不了同 WiFi 下的其它设备。
            # 配了 token 时握手头里的 X-Sinan-Token 必须一字不差匹配才放行。
            if token:
                got = conn.headers.get(b"x-sinan-token", b"").decode("utf-8", "replace")
                if not hmac.compare_digest(got, token):
                    LOG.warning("rejected %s (bad token)", addr[0])
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

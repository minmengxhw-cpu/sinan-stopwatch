#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import socket
import sys
import tempfile
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sinand  # noqa: E402


def handshake_response(token: str, expected: str) -> bytes:
    server_sock, client_sock = socket.socketpair()
    conn = sinand.WSConn(server_sock)
    errors = []

    def run():
        try:
            conn.handshake(expected)
        except Exception as exc:
            errors.append(exc)

    thread = threading.Thread(target=run)
    thread.start()
    request = (
        "GET /sinan HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        f"X-Sinan-Token: {token}\r\n\r\n"
    )
    client_sock.sendall(request.encode())
    response = client_sock.recv(1024)
    thread.join(timeout=2)
    client_sock.close()
    server_sock.close()
    return response


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        token_path = pathlib.Path(tmp) / "bridge.token"
        cfg = {"security": {"token_file": str(token_path)}}
        token = sinand.load_or_create_token(cfg)
        assert len(token) == 64
        assert token_path.stat().st_mode & 0o777 == 0o600
        assert sinand.load_or_create_token(cfg) == token

        assert b"101 Switching Protocols" in handshake_response(token, token)
        assert b"401 Unauthorized" in handshake_response("wrong", token)

        cfg = {
            "daemon": {"path": "/bin:/usr/bin"},
            "programming": {
                "cwd": tmp,
                "codex_cmd": "/bin/echo",
                "codex_transport": "cli",
                "timeout": 5,
            },
        }
        router = sinand.ProgrammingRouter(cfg)
        assert router._paseo_agent_id('{"agentId":"agent-123"}') == "agent-123"
        assert router._paseo_text(
            "[User] test\nPASEO_OK\n[Thought] hidden"
        ) == "PASEO_OK"
        out = router.run("codex", "只回复测试成功")
        assert "只回复测试成功" in out
        assert "禁止删除任何本地文件" in out
        try:
            router.run("happy", "test")
            raise AssertionError("disallowed target accepted")
        except ValueError:
            pass
        assert not list(pathlib.Path(tmp).glob("*.jsonl"))

    print("security and programming voice: ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

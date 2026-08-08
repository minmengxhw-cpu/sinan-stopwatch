#!/usr/bin/env python3
"""Exercise the installed bridge: WAV -> ASR -> ready -> B/send -> Codex."""
from __future__ import annotations

import base64
import hashlib
import json
import os
import pathlib
import socket
import struct
import sys
import time
import wave


GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class Client:
    def __init__(self, token: str):
        self.sock = socket.create_connection(("127.0.0.1", 8790), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        request = (
            "GET /sinan HTTP/1.1\r\nHost: 127.0.0.1:8790\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n"
            f"X-Sinan-Token: {token}\r\n\r\n"
        )
        self.sock.sendall(request.encode())
        response = self._until(b"\r\n\r\n")
        assert b"101 Switching Protocols" in response
        expected = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest())
        assert expected in response

    def _until(self, marker: bytes) -> bytes:
        data = b""
        while marker not in data:
            part = self.sock.recv(4096)
            if not part:
                raise RuntimeError("bridge closed")
            data += part
        return data

    def send(self, obj: dict) -> None:
        payload = json.dumps(obj, ensure_ascii=False).encode()
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            head = struct.pack(">BB", 0x81, 0x80 | n)
        elif n < 65536:
            head = struct.pack(">BBH", 0x81, 0x80 | 126, n)
        else:
            head = struct.pack(">BBQ", 0x81, 0x80 | 127, n)
        masked = bytes(value ^ mask[i % 4] for i, value in enumerate(payload))
        self.sock.sendall(head + mask + masked)

    def recv(self, timeout: float) -> dict:
        self.sock.settimeout(timeout)
        first = self._exact(2)
        opcode = first[0] & 0x0F
        length = first[1] & 0x7F
        if length == 126:
            length = struct.unpack(">H", self._exact(2))[0]
        elif length == 127:
            length = struct.unpack(">Q", self._exact(8))[0]
        if first[1] & 0x80:
            mask = self._exact(4)
        else:
            mask = b""
        payload = self._exact(length)
        if mask:
            payload = bytes(value ^ mask[i % 4] for i, value in enumerate(payload))
        if opcode == 0x9:
            self.send({"t": "pong"})
            return self.recv(timeout)
        return json.loads(payload.decode())

    def _exact(self, count: int) -> bytes:
        data = b""
        while len(data) < count:
            part = self.sock.recv(count - len(data))
            if not part:
                raise RuntimeError("bridge closed")
            data += part
        return data


def wait_for(client: Client, wanted: set[str], seconds: float) -> dict:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        msg = client.recv(max(0.1, deadline - time.monotonic()))
        if msg.get("t") == "voice_status" and msg.get("phase") == "error":
            raise RuntimeError(msg.get("note", "voice error"))
        if msg.get("t") in wanted:
            return msg
    raise TimeoutError(f"no message in {wanted}")


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_live_voice_ws.py <16k-mono-s16.wav>", file=sys.stderr)
        return 2
    token = pathlib.Path("/Users/cheer/.sinan/bridge.token").read_text().strip()
    client = Client(token)
    client.send({"t": "hello", "dev": "acceptance", "ver": "3.1.1"})
    with wave.open(sys.argv[1], "rb") as audio:
        assert audio.getnchannels() == 1 and audio.getsampwidth() == 2
        client.send({"t": "asr_begin", "rate": audio.getframerate(), "target": "codex"})
        seq = 0
        while True:
            pcm = audio.readframes(2048)
            if not pcm:
                break
            client.send({"t": "asr_chunk", "seq": seq,
                         "pcm": base64.b64encode(pcm).decode()})
            seq += 1
    client.send({"t": "asr_end"})
    heard = wait_for(client, {"asr_result"}, 120).get("text", "")
    assert heard
    ready = wait_for(client, {"voice_status"}, 10)
    assert ready.get("phase") == "ready"
    client.send({"t": "voice_send", "target": "codex"})
    reply = wait_for(client, {"say"}, 180).get("text", "")
    assert reply
    print("HEARD:", heard)
    print("REPLY:", reply)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

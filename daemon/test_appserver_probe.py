#!/usr/bin/env python3
"""Small acceptance probe for the official persistent Codex app-server."""
from __future__ import annotations

import json
import subprocess
import sys
import time


def send(proc, obj):
    proc.stdin.write(json.dumps(obj, ensure_ascii=False) + "\n")
    proc.stdin.flush()


def receive(proc, deadline):
    while time.monotonic() < deadline:
        line = proc.stdout.readline()
        if not line:
            raise RuntimeError(proc.stderr.read().strip() or "app-server proxy closed")
        return json.loads(line)
    raise TimeoutError("app-server response timed out")


def wait_id(proc, wanted, timeout=30):
    deadline = time.monotonic() + timeout
    while True:
        msg = receive(proc, deadline)
        if msg.get("id") == wanted:
            if "error" in msg:
                raise RuntimeError(str(msg["error"]))
            return msg["result"]


def main() -> int:
    codex = "/Users/cheer/.local/bin/codex"
    proc = subprocess.Popen(
        [codex, "-c", 'model_reasoning_effort="low"',
         "app-server", "--stdio"], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1,
    )
    try:
        send(proc, {"id": 1, "method": "initialize", "params": {
            "clientInfo": {"name": "sinan", "title": "SINAN Work", "version": "3.1.1"},
            "capabilities": {"experimentalApi": True},
        }})
        wait_id(proc, 1)
        send(proc, {"method": "initialized", "params": {}})
        send(proc, {"id": 2, "method": "thread/start", "params": {
            "cwd": "/Users/cheer/Documents/Codex/2026-08-08/zhe",
            "model": "gpt-5.6-terra", "approvalPolicy": "never",
            "sandbox": "workspace-write", "ephemeral": True,
        }})
        started = wait_id(proc, 2)
        thread_id = started["thread"]["id"]
        send(proc, {"id": 3, "method": "turn/start", "params": {
            "threadId": thread_id, "effort": "low", "approvalPolicy": "never",
            "input": [{"type": "text", "text": "只回复 CODEX_APP_SERVER_OK，不使用工具。"}],
        }})
        wait_id(proc, 3)
        answer = ""
        deadline = time.monotonic() + 120
        while True:
            msg = receive(proc, deadline)
            if msg.get("method") == "item/completed":
                item = msg.get("params", {}).get("item", {})
                if item.get("type") == "agentMessage" and item.get("text"):
                    answer = item["text"]
            if msg.get("method") == "turn/completed":
                break
        if "CODEX_APP_SERVER_OK" not in answer:
            raise RuntimeError(f"unexpected answer: {answer!r}")
        print(answer)
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == "__main__":
    sys.exit(main())

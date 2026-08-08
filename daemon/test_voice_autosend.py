#!/usr/bin/env python3
"""A 转写、B 发送的编程语音契约自测；不需要设备或 whisper。"""
from __future__ import annotations

import pathlib
import sys
import threading
import types

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sinand  # noqa: E402


class FakeConn:
    def __init__(self):
        self.sent: list[dict] = []
        self.alive = True

    def send_json(self, obj):
        self.sent.append(obj)

    def phases(self):
        return [m.get("phase") for m in self.sent if m.get("t") == "voice_status"]

    def types(self):
        return [m.get("t") for m in self.sent]


FAILED = []


def check(name, cond, detail=""):
    if cond:
        print(f"  PASS  {name}")
    else:
        print(f"  FAIL  {name}  {detail}")
        FAILED.append(name)


def server(reply="CODEX_OK"):
    srv = sinand.Server.__new__(sinand.Server)
    srv.programming = types.SimpleNamespace(run=lambda target, text, cancelled=None: reply)
    return srv


def session(heard="修复测试", target="codex"):
    return types.SimpleNamespace(transcribe=lambda: heard, target=target)


def main() -> int:
    print("A 转写 / B 发送契约")
    srv = server()
    conn = FakeConn()
    pending = {"target": "", "text": ""}
    lock = threading.Lock()

    srv._finish_transcription(conn, session(), pending, lock)
    check("A 停止后只做转写",
          conn.types() == ["voice_status", "asr_result", "voice_status"], conn.types())
    check("转写停在 ready", conn.phases() == ["transcribing", "ready"], conn.phases())
    check("B 之前没有运行工具", "say" not in conn.types())
    check("待发送文本绑定 Codex", pending == {"target": "codex", "text": "修复测试"}, pending)

    srv._run_programming(conn, "codex", pending, lock)
    check("B 后才运行并回结果", conn.types()[-2:] == ["voice_status", "say"], conn.types())
    check("运行状态为 running", conn.phases()[-1] == "running", conn.phases())
    check("发送后清空待发送文本", pending == {"target": "", "text": ""}, pending)

    wrong = FakeConn()
    pending = {"target": "codex", "text": "test"}
    srv._run_programming(wrong, "grok", pending, lock)
    check("目标不一致拒绝发送", wrong.phases() == ["error"], wrong.phases())
    check("拒绝时不消费原文本", pending["text"] == "test", pending)

    empty = FakeConn()
    pending = {"target": "", "text": ""}
    srv._finish_transcription(empty, session(heard=""), pending, lock)
    check("空转写不进入 ready", empty.phases() == ["transcribing"], empty.phases())

    cfg = (pathlib.Path(__file__).parent / "config.example.toml").read_text()
    check("配置没有语音记录 inbox", "[inbox]" not in cfg)
    check("Work 只配置三个目标", cfg.count("[[worker]]") == 3)
    check("离线 ASR 使用已安装模型", "ggml-large-v3-turbo-q5_0.bin" in cfg)
    check("ASR 不丢弃前面的语音分段", "tail -1" not in cfg)

    firmware_voice = (pathlib.Path(__file__).parents[1] / "main" / "sinan" / "voice.cpp").read_text()
    daemon_source = (pathlib.Path(__file__).parent / "sinand.py").read_text()
    check("取消录音不会产生迟到转写", "send_audio_cancel" in firmware_voice and
          't == "asr_cancel"' in daemon_source)
    check("Codex 使用电脑常驻会话", "class CodexAppServer" in daemon_source and
          'codex_transport", "app-server"' in daemon_source)
    check("设备断线会取消后台编程任务", "lambda: not conn.alive" in daemon_source)

    print()
    if FAILED:
        print(f"{len(FAILED)} FAILED: {', '.join(FAILED)}")
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

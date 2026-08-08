#!/usr/bin/env python3
"""Run the production VoiceSession against a real 16 kHz mono WAV file."""
from __future__ import annotations

import pathlib
import sys
import wave

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sinand  # noqa: E402


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_live_asr.py <16k-mono-s16.wav>", file=sys.stderr)
        return 2

    wav_path = pathlib.Path(sys.argv[1]).resolve()
    with wave.open(str(wav_path), "rb") as audio:
        assert audio.getnchannels() == 1
        assert audio.getsampwidth() == 2
        session = sinand.VoiceSession(sinand.load_config())
        session.begin(audio.getframerate(), "codex")
        session.pcm.extend(audio.readframes(audio.getnframes()))

    text = session.transcribe()
    if not text:
        print("live ASR returned no text", file=sys.stderr)
        return 1
    print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

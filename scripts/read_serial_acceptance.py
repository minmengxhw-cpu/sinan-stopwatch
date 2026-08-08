#!/usr/bin/env python3
"""Capture a bounded boot log from the connected M5StopWatch."""
from __future__ import annotations

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbmodem2101")
    parser.add_argument("--seconds", type=float, default=14)
    parser.add_argument("--summary", action="store_true")
    parser.add_argument("--photos", type=int, default=2)
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=0.2) as device:
        device.dtr = False
        device.rts = True
        time.sleep(0.1)
        device.rts = False
        deadline = time.monotonic() + args.seconds
        chunks = []
        while time.monotonic() < deadline:
            part = device.read(4096)
            if part:
                chunks.append(part)
    text = b"".join(chunks).decode("utf-8", "replace")
    if not args.summary:
        print(text)
    required = ["Project name:     sinan", "App version:      3.1.1",
                f"sinan.photo: found {args.photos} photos", "[Photos] on open"]
    missing = [item for item in required if item not in text]
    forbidden = ["Guru Meditation", "assert failed", "abort()"]
    bad = [item for item in forbidden if item in text]
    if missing or bad:
        raise RuntimeError(f"serial acceptance failed; missing={missing}, forbidden={bad}")
    print(f"serial acceptance: v3.1.1, {args.photos} photos, Photos opened, no crash")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

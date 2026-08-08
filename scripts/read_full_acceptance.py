#!/usr/bin/env python3
"""Bounded post-flash acceptance for the complete SINAN firmware."""
from __future__ import annotations

import argparse
import time

import serial


REQUIRED = [
    "Project name:     sinan",
    "App version:      4.2.0",
    "[Photos] on create, embedded photos=2",
    "[Agarwood] on create, agarwood ritual ready",
    "[Buddy] on create",
    "[Work] on create",
    "[Tools] on create",
    "[Connect] on create",
    "[Settings] on create",
    "[Stopwatch] on create",
    "[Badge] on create",
    "DOGPHOTO: render index=1/2 name=2020 / PUPPY",
    "result=OK",
]
FORBIDDEN = ["Guru Meditation", "assert failed", "abort()", "LoadProhibited", "StoreProhibited"]
ABSENT = ["[Dog Play] on create", "[Today] on create", "[Wenwan] on create", "[AlarmClock] on create"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="/dev/cu.usbmodem2101")
    parser.add_argument("--seconds", type=float, default=18)
    parser.add_argument("--output")
    args = parser.parse_args()

    # The ESP32-S3 USB-Serial/JTAG port is reset separately by esptool.  Keep
    # both control lines idle here so opening the port does not force UART0
    # download mode on this board.
    device = serial.Serial()
    device.port = args.port
    device.baudrate = 115200
    device.timeout = 0.2
    device.dtr = False
    device.rts = False
    device.open()
    with device:
        deadline = time.monotonic() + args.seconds
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            part = device.read(4096)
            if part:
                chunks.append(part)

    text = b"".join(chunks).decode("utf-8", "replace")
    if args.output:
        with open(args.output, "w", encoding="utf-8") as handle:
            handle.write(text)

    missing = [item for item in REQUIRED if item not in text]
    forbidden = [item for item in FORBIDDEN if item in text]
    unexpected = [item for item in ABSENT if item in text]
    if missing or forbidden or unexpected:
        raise RuntimeError(
            f"serial acceptance failed; missing={missing}, forbidden={forbidden}, unexpected={unexpected}"
        )

    print("serial acceptance: v4.2.0, photos + agarwood + Work/Buddy routes created, no crash")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""截屏：发 P，收 SHOT BEGIN/END 之间的 base64，存 PNG（有 Pillow）或 .rgb565。

用法: python3 tools/screenshot.py /dev/cu.usbmodemXXXX [out.png]
"""
import base64
import sys
import time

import serial


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else f"shot_{int(time.time())}.png"

    with serial.Serial(port, 115200, timeout=120) as ser:
        ser.write(b"P\n")
        w = h = None
        chunks = []
        while True:
            line = ser.readline().decode(errors="replace").strip()
            if not line:
                continue
            if line.startswith("SHOT BEGIN"):
                _, _, w, h, *_ = line.split()
                w, h = int(w), int(h)
            elif line.startswith("SHOT END"):
                break
            elif line.startswith("SHOT FAIL"):
                print("device failed to snapshot")
                return 1
            elif w is not None:
                chunks.append(line)

    raw = base64.b64decode("".join(chunks))
    print(f"got {len(raw)} bytes ({w}x{h} rgb565le)")

    try:
        from PIL import Image

        img = Image.frombytes("RGB", (w, h), raw, "raw", "BGR;16")
        img.save(out)
        print(f"saved {out}")
    except ImportError:
        raw_out = out + ".rgb565"
        with open(raw_out, "wb") as f:
            f.write(raw)
        print(f"Pillow not installed, raw saved to {raw_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

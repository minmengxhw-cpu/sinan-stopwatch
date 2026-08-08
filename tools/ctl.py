#!/usr/bin/env python3
"""司南串口控制台。交互 REPL，也可单发：

  python3 tools/ctl.py /dev/cu.usbmodemXXXX            # REPL
  python3 tools/ctl.py /dev/cu.usbmodemXXXX S          # 单发一条
  python3 tools/ctl.py /dev/cu.usbmodemXXXX I approval # 注入假审批

命令见 main/sinan/debug_cli.h 头注释。
"""
import sys
import time

import serial


def open_ser(port: str) -> serial.Serial:
    return serial.Serial(port, 115200, timeout=2)


def drain(ser: serial.Serial, quiet_ms: int = 300) -> None:
    """把设备回显读到安静为止。"""
    last = time.time()
    while (time.time() - last) * 1000 < quiet_ms:
        line = ser.readline()
        if line:
            last = time.time()
            sys.stdout.write(line.decode(errors="replace"))
            sys.stdout.flush()


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    port = sys.argv[1]

    with open_ser(port) as ser:
        if len(sys.argv) > 2:
            ser.write((" ".join(sys.argv[2:]) + "\n").encode())
            drain(ser)
            return 0

        print("sinan ctl. 输入命令，quit 退出。")
        while True:
            try:
                cmd = input("sinan> ").strip()
            except (EOFError, KeyboardInterrupt):
                break
            if cmd in ("quit", "exit"):
                break
            if not cmd:
                continue
            ser.write((cmd + "\n").encode())
            drain(ser)
    return 0


if __name__ == "__main__":
    sys.exit(main())

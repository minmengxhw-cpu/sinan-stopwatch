#!/usr/bin/env python3
"""从 main/sinan/design.h 导出设计令牌到 web/prototype/tokens.json。

web 原型与固件共用同一份色板/半径/时长 —— 改视觉只改 design.h，
然后重跑这个脚本。CI 可以 diff 两者哈希来防止走样。
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "main" / "sinan" / "design.h"
OUT = ROOT / "web" / "prototype" / "tokens.json"

RE_HEX = re.compile(r"constexpr\s+uint32_t\s+(\w+)\s*=\s*0x([0-9A-Fa-f]{6})")
RE_INT = re.compile(r"constexpr\s+int\s+(\w+)\s*=\s*(\d+)")
RE_MS = re.compile(r"constexpr\s+uint32_t\s+(\w+)\s*=\s*(\d+)\b")


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    colors = {m.group(1): "#" + m.group(2).lower() for m in RE_HEX.finditer(text)}
    ints = {m.group(1): int(m.group(2)) for m in RE_INT.finditer(text)}
    times = {m.group(1): int(m.group(2)) for m in RE_MS.finditer(text)
             if m.group(1).startswith(("T_", "PRECESS", "JITTER", "LONGPRESS", "STALE"))}

    tokens = {"colors": colors, "geometry": ints, "motion": times}
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(tokens, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {OUT} ({len(colors)} colors, {len(ints)} geometry, {len(times)} motion)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

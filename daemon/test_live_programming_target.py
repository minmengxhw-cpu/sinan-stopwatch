#!/usr/bin/env python3
"""Run one configured programming target for real acceptance evidence."""
from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sinand  # noqa: E402


def main() -> int:
    if len(sys.argv) != 2 or sys.argv[1] not in sinand.ProgrammingRouter.TARGETS:
        print("usage: test_live_programming_target.py codex|paseo|grok", file=sys.stderr)
        return 2
    target = sys.argv[1]
    router = sinand.ProgrammingRouter(sinand.load_config())
    reply = router.run(target, f"只回复 SINAN_{target.upper()}_OK，不使用工具。")
    expected = f"SINAN_{target.upper()}_OK"
    if expected not in reply:
        raise RuntimeError(f"unexpected {target} reply: {reply!r}")
    print(reply)
    return 0


if __name__ == "__main__":
    sys.exit(main())

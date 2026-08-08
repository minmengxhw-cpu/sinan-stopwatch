#!/usr/bin/env python3
"""Exercise the production CodexAppServer class from a worker thread."""
from __future__ import annotations

import pathlib
import sys
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sinand  # noqa: E402


def main() -> int:
    cfg = sinand.load_config()
    router = sinand.ProgrammingRouter(cfg)
    result = []
    errors = []

    def run():
        try:
            router.warm_codex()
            result.append(router.run("codex", "只回复 ROUTER_APP_SERVER_OK，不使用工具。"))
        except Exception as exc:
            errors.append(str(exc))

    worker = threading.Thread(target=run)
    worker.start()
    worker.join(timeout=180)
    if worker.is_alive():
        raise RuntimeError("worker did not finish")
    if errors:
        raise RuntimeError(errors[0])
    if not result or "ROUTER_APP_SERVER_OK" not in result[0]:
        raise RuntimeError(f"unexpected result: {result!r}")
    print(result[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())

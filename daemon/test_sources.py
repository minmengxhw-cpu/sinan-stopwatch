#!/usr/bin/env python3
from __future__ import annotations

import json
import pathlib
import sys
import tempfile
from datetime import date

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sinand  # noqa: E402


def main() -> int:
    firmware_main = (pathlib.Path(__file__).parents[1] / "main" / "main.cpp").read_text()
    assert 'route::open(SN_BOOT_APP)' in firmware_main
    assert 'bridge.configured() ? SN_BOOT_APP : "Connect"' not in firmware_main

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        archive = root / "numbers"
        archive.mkdir()
        (archive / f"{date.today().isoformat()}.md").write_text(
            "# 今日观察号\n\n## 号码：全球流动性\n\n"
            "- **等级：C · 观察号**\n"
            "- **一句话：** 只做研究线索，不代表价格方向。\n",
            encoding="utf-8",
        )
        almanac = sinand.Almanac({"almanac": {"number_archive": str(archive)}})
        snapshot = almanac.snapshot()
        assert snapshot["number"]["title"] == "全球流动性"
        assert snapshot["number"]["code"] == "C · 观察号"

        kb = root / "kb.json"
        kb.write_text(json.dumps({
            "default_search_layer": "seventhwave",
            "combined_layers": {"seventhwave_chunks": 104493},
            "warnings": [],
        }), encoding="utf-8")
        fleet = sinand.Fleet({"daemon": {"path": ""}, "worker": [{
            "id": "kb", "label": "KB", "kind": "knowledge_base", "status_file": str(kb)
        }]})
        worker = fleet.snapshot()["workers"][0]
        assert worker["state"] == "idle"
        assert "104493" in worker["task"]

    print("real local sources: ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())

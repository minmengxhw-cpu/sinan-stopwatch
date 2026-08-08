#!/usr/bin/env bash
# 从官方 UserDemo 同步 HAL / launcher / assets。这些文件我们不改，只跟着上游走。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git clone --depth 1 https://github.com/m5stack/M5StopWatch-UserDemo.git "$TMP/up"

rsync -a --delete "$TMP/up/main/hal/"    "$ROOT/main/hal/"
rsync -a          "$TMP/up/main/assets/" "$ROOT/main/assets/"
mkdir -p "$ROOT/main/apps/common" "$ROOT/main/apps/app_launcher" "$ROOT/main/apps/app_setup"
rsync -a --delete "$TMP/up/main/apps/common/"       "$ROOT/main/apps/common/"
rsync -a --delete "$TMP/up/main/apps/app_launcher/" "$ROOT/main/apps/app_launcher/"
rsync -a --delete "$TMP/up/main/apps/app_setup/"    "$ROOT/main/apps/app_setup/"
cp "$TMP/up/repos.json" "$TMP/up/fetch_repos.py" "$TMP/up/partitions.csv" "$ROOT/"
[ -d "$TMP/up/patches" ] && rsync -a "$TMP/up/patches/" "$ROOT/patches/"

echo "synced. 别忘了跑 python3 fetch_repos.py"

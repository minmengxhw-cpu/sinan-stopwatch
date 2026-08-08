#!/usr/bin/env bash
# 把 sinand 装成 launchd 常驻服务。开机自启，崩了自动拉起。
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="$HOME/.sinan/app"
PLIST="$HOME/Library/LaunchAgents/com.sinan.daemon.plist"

mkdir -p "$APP" "$HOME/Library/LaunchAgents"
install -m 0644 "$HERE/sinand.py" "$APP/sinand.py"
install -m 0644 "$HERE/config.example.toml" "$APP/config.example.toml"
install -m 0644 "$HERE/com.sinan.daemon.plist" "$PLIST"

# Existing config/token are deliberately preserved; config migration is explicit.
launchctl bootstrap "gui/$(id -u)" "$PLIST"
echo "SINAN bridge installed. Log: $HOME/.sinan/sinand.log"

#!/usr/bin/env bash
# 把 sinand 装成 launchd 常驻服务。开机自启，崩了自动拉起。
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLIST="$HOME/Library/LaunchAgents/com.sinan.daemon.plist"
PY="$(command -v python3)"

if [[ "$($PY -c 'import sys;print(sys.version_info>=(3,11))')" != "True" ]]; then
  echo "need python 3.11+ (tomllib)"; exit 1
fi

mkdir -p "$HOME/.sinan" "$HOME/Library/LaunchAgents"

cat > "$PLIST" <<PLIST_EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>com.sinan.daemon</string>
  <key>ProgramArguments</key><array>
    <string>$PY</string><string>$HERE/sinand.py</string>
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>/tmp/sinand.log</string>
  <key>StandardErrorPath</key><string>/tmp/sinand.log</string>
</dict></plist>
PLIST_EOF

launchctl unload "$PLIST" 2>/dev/null || true
launchctl load "$PLIST"
echo "loaded. log: /tmp/sinand.log"
echo "本机 IP（填进固件 config.h 的 SN_WS_URI）: $(ipconfig getifaddr en0 2>/dev/null || echo '?')"

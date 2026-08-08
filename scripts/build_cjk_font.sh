#!/usr/bin/env bash
# 生成中文子集字体。
#
# 用思源宋体不是黑体：宋体的横细竖粗在鎏金色上有金石感，
# 跟司南的器物调性一致；黑体会让整个界面看起来像一个手机 App。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONTS="$ROOT/main/assets/fonts"
SRC="${1:-$HOME/Library/Fonts/SourceHanSerifSC-Medium.otf}"

[ -f "$SRC" ] || { echo "找不到字体：$SRC"; echo "用法：$0 /path/to/SourceHanSerifSC-Medium.otf"; exit 1; }
command -v npx >/dev/null || { echo "需要 node/npx"; exit 1; }

SYMBOLS="$(tr -d '\n ' < "$FONTS/sinan_cjk_subset.txt")"

for SIZE in 28 40; do
  npx -y lv_font_conv \
    --font "$SRC" --size "$SIZE" --bpp 4 --format lvgl \
    --symbols "$SYMBOLS" --no-compress \
    --lv-font-name "lv_font_sinan_serif_${SIZE}" \
    -o "$FONTS/lv_font_sinan_serif_${SIZE}.c"
  echo "生成 lv_font_sinan_serif_${SIZE}.c"
done

echo "重新 idf.py build 即可自动启用（CMakeLists 检测到 .c 文件会打开 SINAN_HAS_CJK_FONT）"

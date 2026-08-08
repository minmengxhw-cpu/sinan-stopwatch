#!/usr/bin/env bash
# 从官方 UserDemo 补齐 HAL / launcher / assets。
#
# 这是非破坏式 hydrate：已有文件不覆盖、不删除。需要升级上游时，先在一个
# 新目录审查差异，再人工合并；同步脚本不能替用户决定哪些本地改动可丢弃。
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UP="${SINAN_UPSTREAM_DIR:-$ROOT/upstream/M5StopWatch-UserDemo}"

if [ ! -d "$UP/.git" ]; then
  mkdir -p "$(dirname "$UP")"
  git clone --depth 1 https://github.com/m5stack/M5StopWatch-UserDemo.git "$UP"
fi

mkdir -p "$ROOT/main/hal" "$ROOT/main/assets"
rsync -a --ignore-existing "$UP/main/hal/"    "$ROOT/main/hal/"
rsync -a --ignore-existing "$UP/main/assets/" "$ROOT/main/assets/"
# 官方应用一并同步。早期版本只拿 launcher 和 setup，等于把秒表、闹钟、
# 徽章上传、以及 IMU/麦克风自检全丢了 —— 而这块板出厂就叫 StopWatch。
# 保留它们不冲突：官方应用画在 lv_screen_active()，司南画在岁差根容器上，
# 根容器在引用计数归零时是隐藏的。
for a in common app_launcher app_setup app_stopwatch app_alarm_clock \
         app_badge app_imu app_fft app_watch_face app_lucky_wheel app_template; do
  mkdir -p "$ROOT/main/apps/$a"
  rsync -a --ignore-existing "$UP/main/apps/$a/" "$ROOT/main/apps/$a/"
done
if [ -d "$UP/patches" ]; then
  mkdir -p "$ROOT/patches"
  rsync -a --ignore-existing "$UP/patches/" "$ROOT/patches/"
fi

echo "hydrated without deleting or overwriting local files. 别忘了跑 python3 fetch_repos.py"

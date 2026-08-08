/*
 * debug_cli.h — 串口调试 CLI。
 *
 * 没到货也能验 UI，到货后能自动自测。行协议，每条命令一行：
 *   P            截屏（SHOT BEGIN/END 包住 base64 RGB565）
 *   a b A B C    注入 A短/B短/A长/B长/A+B
 *   @x,y         注入点按（坐标原点在屏左上）
 *   Q / W / X    跳到 Rest / Work / Action
 *   I approval   注入假审批；I clear 清除；I error|done <text> 注入通知
 *   G            打印 gaze mood + 珠串焦点
 *   H ok|ng|warn|tick|pet|alert|pair|soft   触感试听
 *   S            selftest：heap/psram/ble/hid/ws/layer/mood 一行 JSON
 *   D YYYY-MM-DD HH:MM:SS   对时
 */
#pragma once

namespace sinan::debug {

void start();   // 建串口读行任务，main.cpp 调一次

}  // namespace sinan::debug

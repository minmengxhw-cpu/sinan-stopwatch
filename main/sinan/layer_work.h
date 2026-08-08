/*
 * layer_work.h — 阵。工作层：agent 环 + focus。
 *
 * 圆周按 worker 数均分，段弧长度 = 剩余额度，颜色 = 状态。
 * 与旧 app_fleet 的差异：它不再只是看板 —— A 对 focus 发 HID OK，
 * focus 与 Rest 项圈上的 agent 珠共用同一个索引。
 */
#pragma once
#include "shell.h"

namespace sinan::layer_work {

void create();
void enter();
void exit();
void tick(uint32_t now);

bool key(shell::Key k);
void rim_slide(float delta_deg, bool fast, uint32_t now);
void tap_center(uint32_t now);

}  // namespace sinan::layer_work

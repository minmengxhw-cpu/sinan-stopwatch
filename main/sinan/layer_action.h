/*
 * layer_action.h — Action 层。
 *
 * 六等分：FAST / NG / OK / PLAN / AI 五段弧 + 中央对讲。
 * 朱砂导轨指向 B 侧（NG），石绿导轨指向 A 侧（OK）——
 * 把物理键和屏上动作用颜色缝起来，学 vibewatch 的语义，用司南的色。
 */
#pragma once
#include "shell.h"

namespace sinan::layer_action {

void create();
void enter();
void exit();
void tick(uint32_t now);

bool key(shell::Key k);
void tap_polar(float deg, float r, uint32_t now);

}  // namespace sinan::layer_action

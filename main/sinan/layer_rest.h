/*
 * layer_rest.h — 寐。默认层，设备 90% 的时间停在这里。
 *
 * 组成：团团照片（身体）+ 点阵幽灵（灵魂）+ 项圈珠串（今天的因果）。
 * 没有秒针，没有版式轮询。想知道时间：敲一下桌子，或点一下中心。
 */
#pragma once
#include "shell.h"

namespace sinan::layer_rest {

void create();            // shell::init 调一次，挂在岁差根下
void enter();             // 显示（不切走时它一直在底层）
void exit();              // 隐藏，不销毁，照片不重新解码
void tick(uint32_t now);  // 仅当它是当前层时被调

bool key(shell::Key k);
void rim_slide(float delta_deg, bool fast, uint32_t now);
void tap_center(uint32_t now);
void imu_tap(uint32_t now);   // 敲击唤醒
void next_photo();            // B 按住 + 点屏：换一张

}  // namespace sinan::layer_rest

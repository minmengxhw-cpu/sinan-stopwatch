/*
 * layer_interrupt.h — 全局中断层。
 *
 * 四种脸，共用同一圈 Rim：
 *   Pending  审批：整圈点亮按剩余时间收缩，色随危险等级
 *   Settled  落定：弧从中线张开成整圈，石绿或朱砂，一瞬后退出
 *   Notice   ERROR / DONE 通知（去重后才有资格全屏）
 *   Pairing  6 位配对码大字 —— DisplayOnly 的码不上屏，链路就永远配不上
 *
 * 它可被任何层打断任何人，也只能被 shell 推上台。
 */
#pragma once
#include "shell.h"

namespace sinan::layer_interrupt {

void create();
void enter();
void exit();

// shell 每帧调。返回 true 表示中断已了结，可以退回之前的层
bool tick(uint32_t now);

bool key(shell::Key k);

// 当前是否有 Grave 审批在展示（shell 用来压 Done 通知）
bool showing_grave();

}  // namespace sinan::layer_interrupt

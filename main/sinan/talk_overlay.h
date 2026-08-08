/*
 * talk_overlay.h — 对讲覆盖层。
 *
 * 中央长按从任何层进来都是同一张脸：Rim 石绿倒计时 + 20 段电平环。
 * 录音走 voice → WS → daemon 转写；daemon 若配了 inject，文本直接
 * 打进焦点窗口（haosuo 路线），否则走 agent_cmd 回播。
 */
#pragma once
#include <cstdint>

namespace sinan::talk {

void create();            // shell::init 调一次
void begin(uint32_t now); // 中心长按触发
void end();               // 松手
void abort();
bool active();
void tick(uint32_t now);  // shell 每帧调，内部自己控节奏

}  // namespace sinan::talk

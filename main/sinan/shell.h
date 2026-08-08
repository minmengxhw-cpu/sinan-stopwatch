/*
 * shell.h — 四层壳。
 *
 * 职责边界：全机唯一的层切换与输入路由。层是挂在岁差根容器下的子树，
 * 不是互相 open/close 抢屏的 App —— 切层只是显隐，照片不重新解码。
 *
 *   L3 Interrupt  审批 / 配对 / ERROR / DONE（可抢占任何层）
 *   L2 Action     FAST / NG / OK / PLAN / AI / 中央对讲
 *   L1 Work       阵：≤6 agent 极坐标环 + focus
 *   L0 Rest       寐：团团身体 + 灵魂点阵 + 项圈珠串（默认）
 *
 * 键位语义见 input_map.h，全机唯一。
 */
#pragma once
#include "state.h"
#include <lvgl.h>
#include <cstdint>

namespace sinan::shell {

enum class Layer : uint8_t { Rest, Work, Action, Interrupt };

// 路由给层的按键事件。语义恒定，见 input_map.h
enum class Key : uint8_t {
    AShort,
    ALong,
    BShort,
    BLong,
    Combo,       // A+B 同按
    TapCenter,   // 中心短触
};

void init();                 // 建全部层（一次），默认进 Rest
void tick(uint32_t now);     // AppSinan::onRunning 每帧调
Layer current();

// Settings 打开期间 shell 让出输入。main.cpp 安装后登记一次
void set_settings_app_id(int id);

// Work 层 focus 与珠串 agent 珠共用同一个索引
int work_focus();
void set_work_focus(int i);

/* ---- debug_cli 注入（串口无硬件调试） ---- */
void inject_key(char k);                 // 'a' 'b' 'A'(长) 'B'(长) 'C'(A+B)
void inject_touch(int x, int y, bool down);
void inject_layer(int l);                // 0 Rest / 1 Work / 2 Action

/* ---- 层与层之间要说的话 ---- */
void on_decision_made(bool approved, uint32_t now);  // Interrupt → Rest 的 Praise 回声

}  // namespace sinan::shell

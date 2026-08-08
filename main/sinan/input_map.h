/*
 * input_map.h — 全机唯一键位表。
 *
 * 任何层、任何模块不得私自重定义 A/B/触摸/敲击的语义。
 * 改交互只改这里与 shell.cpp 的路由。
 *
 * | 输入             | 全局语义       | Rest            | Work           | Action      | Interrupt        |
 * |------------------|----------------|-----------------|----------------|-------------|------------------|
 * | A 短             | 肯定 / 前进    | （轻振，无操作）| 对 focus 发 OK | 发 OK       | 普通允许         |
 * | A 长 0.8s        | 危险确认/深度  | 锁定当前照片    | —              | —           | Grave 批准       |
 * | B 短             | 否定 / 切换    | —               | 下一个 agent   | 发 NG       | 拒绝             |
 * | B 长             | 回退           | → Settings      | → Rest         | → Work      | 取消通知回下层   |
 * | A+B 同按         | 层切换         | → Work          | → Action       | → Work      | 忽略             |
 * | Rim 快滑         | 盘珠 / 切换    | 拨珠一格        | 切 focus       | —           | —                |
 * | Rim 慢滑         | 抚触           | Pet             | —              | —           | —                |
 * | 敲击 TAP_G       | 唤醒           | 露时 6s+提亮    | 同左           | 同左        | 忽略             |
 * | 中心短触         | Peek / 选中    | 露时 6s         | 选中 agent     | —           | —                |
 * | 中心长按 ≥0.5s   | 对讲           | 对讲            | 对讲           | 对讲        | —                |
 * | B 按住 + 点屏    | 换照片         | 下一张          | —              | —           | —                |
 */
#pragma once
#include <cstdint>

namespace sinan::input_map {

constexpr uint32_t COMBO_MS      = 300;   // A+B 同时按住多久算层切换
constexpr uint32_t TALK_HOLD_MS  = 500;   // 中心长按多久进入对讲
constexpr int      TALK_RADIUS   = 60;    // 中心对讲的命中半径（px）
constexpr int      CENTER_TAP_R  = 150;   // 中心短触的命中半径（px）
constexpr float    BEAD_STEP_DEG = 20.0f; // 拨珠一格的角度（= 360 / 18 珠）
constexpr float    SLIDE_FAST_DPS = 240.0f; // 角速度高于此 = 盘珠，低于此且持续 = 抚触

}  // namespace sinan::input_map

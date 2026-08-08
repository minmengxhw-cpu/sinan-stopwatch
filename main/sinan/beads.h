/*
 * beads.h — 项圈珠串。
 *
 * 隐喻（不许改味）：团团项圈上的珠 = 今天你与 agent 之间的因果。
 * 不是装饰 sin()，不是秒针。没有数据的空槽是一串未开光的珠，
 * 是邀请，不是错误。
 *
 * 职责边界：数据重建规则 + Rim 轨道上的绘制。交互手势由 Rest 层
 * 判别后调 rotate / set_focus，这里只管「现在哪颗是焦点、长什么样」。
 */
#pragma once
#include "state.h"
#include <lvgl.h>
#include <cstdint>

namespace sinan::beads {

constexpr int kMax = 18;   // 视觉上限；再多在 466 圆上就糊了

enum class BeadKind : uint8_t {
    Empty,      // 漆色空槽
    Approve,    // 今日批准（计数珠）
    Deny,       // 今日拒绝（计数珠）
    AgentRun,   // 某 worker 在 run
    AgentStall, // stall
    QuotaLow,   // quota < 0.10
    AgentIdle,  // 在线但闲着
    Day,        // 日课 / 号码
};

struct Bead {
    BeadKind kind = BeadKind::Empty;
    uint32_t color = 0;        // 来自 design 语义色，重建时定
    char label[12] = {};       // 拨到时弦区显示
    char detail[40] = {};      // 次级说明
    uint8_t agent_idx = 0xFF;  // 对应 FleetState.workers 下标，0xFF = 无
};

// 由 shell 周期驱动。确定性顺序：agent 珠 → 计数珠 → Day 珠 → 空槽
void rebuild_from(const Snapshot& s);

int count();
const Bead& get(int i);
int focus();
void set_focus(int i);
void rotate(int delta);        // 盘珠 ±1，焦点绕环走

// 绘制：在 parent 上建 kMax 个小圆点，极坐标 R_RIM。只建一次
void mount(lv_obj_t* parent);
void unmount();
void redraw();                 // 只改颜色/opa/尺寸，不销毁

// Dream 状态的极慢自转：整体相位每 60s 挪一格。非 Dream 不调用
void dream_step();

}  // namespace sinan::beads

/*
 * beads.cpp
 *
 * 重建规则（确定性，便于测试与复盘）：
 *   1. 最多 6 颗 agent 珠：run/stall/低额度优先，idle 暗鎏金
 *   2. 今日批准 / 拒绝各压成一颗计数珠（A12 / D3）
 *   3. almanac 有号码时一颗 Day 珠
 *   4. 尾部空槽：漆色，不动
 */
#include "beads.h"
#include "design.h"
#include "ring.h"
#include <cstdio>
#include <cstring>

namespace sinan::beads {
namespace {

using namespace design;

Bead s_beads[kMax];
int s_count = 0;        // 非空槽数量（含 agent/day/tally），尾部是 Empty
int s_focus = 0;
int s_phase = 0;        // Dream 自转相位（格）

lv_obj_t* s_dots[kMax] = {};
lv_obj_t* s_parent = nullptr;
bool s_breathing[kMax] = {};   // 呼吸动画去抖：只在状态变化时重建

constexpr float kSpanDeg = 360.0f / kMax;

uint32_t color_of(BeadKind k)
{
    switch (k) {
        case BeadKind::Approve:    return MALACHITE;
        case BeadKind::Deny:       return CINNABAR;
        case BeadKind::AgentRun:   return MALACHITE;
        case BeadKind::AgentStall: return AMBER;
        case BeadKind::QuotaLow:   return AMBER;
        case BeadKind::AgentIdle:  return BRONZE_D;
        case BeadKind::Day:        return BRONZE;
        default:                   return LACQUER;
    }
}

void put(BeadKind k, const char* label, const char* detail, uint8_t agent_idx)
{
    if (s_count >= kMax) return;
    Bead& b = s_beads[s_count++];
    b.kind = k;
    b.color = color_of(k);
    std::snprintf(b.label, sizeof(b.label), "%s", label ? label : "");
    std::snprintf(b.detail, sizeof(b.detail), "%s", detail ? detail : "");
    b.agent_idx = agent_idx;
}

}  // namespace

void rebuild_from(const Snapshot& s)
{
    s_count = 0;

    // 1. agent 珠：按 workers 顺序，状态决定色
    for (int i = 0; i < s.fleet.count && i < FleetState::kMax; i++) {
        const Worker& w = s.fleet.workers[i];
        BeadKind k = BeadKind::AgentIdle;
        if (w.state == WorkerState::Run)        k = BeadKind::AgentRun;
        else if (w.state == WorkerState::Stall) k = BeadKind::AgentStall;
        else if (w.state == WorkerState::Down)  continue;  // 离线不上串
        if (w.quota < 0.10f && w.state != WorkerState::Down) k = BeadKind::QuotaLow;
        put(k, w.label.c_str(), w.task.c_str(), static_cast<uint8_t>(i));
    }

    // 2. 今日计数珠
    if (s.tally.approved > 0) {
        char lb[12];
        std::snprintf(lb, sizeof(lb), "A%u", static_cast<unsigned>(s.tally.approved));
        put(BeadKind::Approve, lb, "approved today", 0xFF);
    }
    if (s.tally.denied > 0) {
        char lb[12];
        std::snprintf(lb, sizeof(lb), "D%u", static_cast<unsigned>(s.tally.denied));
        put(BeadKind::Deny, lb, "denied today", 0xFF);
    }

    // 3. Day 珠
    if (!s.almanac.number_code.empty()) {
        put(BeadKind::Day, s.almanac.number_code.c_str(),
            s.almanac.number_title.c_str(), 0xFF);
    }

    // 4. 空槽
    while (s_count < kMax) {
        s_beads[s_count] = Bead{};
        s_beads[s_count].color = color_of(BeadKind::Empty);
        s_count++;
    }
    s_count = kMax;  // 槽位恒定 18，Empty 也是槽

    if (s_focus >= kMax) s_focus = 0;
    redraw();
}

int count() { return kMax; }
const Bead& get(int i) { return s_beads[i % kMax]; }
int focus() { return s_focus; }

void set_focus(int i)
{
    s_focus = ((i % kMax) + kMax) % kMax;
    redraw();
}

void rotate(int delta)
{
    set_focus(s_focus + delta);
}

void mount(lv_obj_t* parent)
{
    if (s_parent == parent) return;
    unmount();
    s_parent = parent;

    for (int i = 0; i < kMax; i++) {
        lv_obj_t* d = lv_obj_create(parent);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_pad_all(d, 0, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        s_dots[i] = d;
    }
    redraw();
}

void unmount()
{
    for (int i = 0; i < kMax; i++) {
        if (s_dots[i]) {
            lv_obj_delete(s_dots[i]);
            s_dots[i] = nullptr;
        }
        s_breathing[i] = false;
    }
    s_parent = nullptr;
}

void redraw()
{
    if (!s_parent) return;

    for (int i = 0; i < kMax; i++) {
        lv_obj_t* d = s_dots[i];
        if (!d) continue;
        const Bead& b = s_beads[i];
        const bool focused = (i == s_focus);

        // 焦点珠略大并勾一道生宣边，其余 10px
        const int sz = focused ? 13 : 10;
        lv_obj_set_size(d, sz, sz);

        lv_obj_set_style_bg_color(d, design::c(b.color), 0);
        // 空槽压暗到近乎熄灭；焦点珠常亮
        const lv_opa_t opa = (b.kind == BeadKind::Empty)
                                 ? LV_OPA_60
                                 : (focused ? LV_OPA_COVER : LV_OPA_90);
        lv_obj_set_style_bg_opa(d, opa, 0);

        if (focused && b.kind != BeadKind::Empty) {
            lv_obj_set_style_border_width(d, 1, 0);
            lv_obj_set_style_border_color(d, design::c(SILK), 0);
        } else {
            lv_obj_set_style_border_width(d, 0, 0);
        }

        // run / 低额度珠做 opa 呼吸（T_BREATH 下限），其余静止
        const bool want_breathe =
            (b.kind == BeadKind::AgentRun || b.kind == BeadKind::QuotaLow) && !focused;
        if (want_breathe != s_breathing[i]) {
            s_breathing[i] = want_breathe;
            if (want_breathe) ui::breathe(d, T_BREATH, 90, 255);
            else ui::stop_breathe(d);
        }

        const float deg = i * kSpanDeg + s_phase * kSpanDeg;
        ui::place_polar(d, R_RIM, deg);
    }
}

void dream_step()
{
    s_phase = (s_phase + 1) % kMax;
    redraw();
}

}  // namespace sinan::beads

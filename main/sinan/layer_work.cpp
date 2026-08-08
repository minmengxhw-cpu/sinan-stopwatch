/*
 * layer_work.cpp — 阵。
 *
 * 数据每秒最多重画一次。弧动画本身有 320ms，画太勤反而看着抖。
 * 断 WS 不弹窗：Rim 转靛青，人看得懂。
 */
#include "layer_work.h"
#include "bridge_hid.h"
#include "design.h"
#include "haptics.h"
#include "precession.h"
#include "ring.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cmath>
#include <cstdio>

namespace sinan::layer_work {

using namespace design;
namespace ui = sinan::ui;
using mooncake::mclog;

namespace {

constexpr int kMaxSeg = FleetState::kMax;
constexpr float kGapDeg = 6.0f;  // 段间留白。没有留白就看不出是几段

lv_obj_t* s_stage = nullptr;
lv_obj_t* s_rim   = nullptr;
lv_obj_t* s_seg[kMaxSeg] = {};
lv_obj_t* s_seg_bg[kMaxSeg] = {};
lv_obj_t* s_seg_label[kMaxSeg] = {};
lv_obj_t* s_focus_pct  = nullptr;
lv_obj_t* s_focus_name = nullptr;
lv_obj_t* s_chord = nullptr;

uint32_t s_last_draw = 0;

uint32_t hue_of(WorkerState s)
{
    switch (s) {
        case WorkerState::Run:   return MALACHITE;
        case WorkerState::Idle:  return BRONZE;
        case WorkerState::Stall: return AMBER;
        default:                 return INDIGO;
    }
}

const char* name_of(WorkerState s)
{
    switch (s) {
        case WorkerState::Run:   return "running";
        case WorkerState::Idle:  return "idle";
        case WorkerState::Stall: return "stalled";
        default:                 return "offline";
    }
}

void rebuild(const FleetState& f)
{
    const int n = f.count > 0 ? f.count : 1;
    const float span = 360.0f / n;
    const int focus = shell::work_focus();

    for (int i = 0; i < kMaxSeg; i++) {
        const bool live = i < f.count;
        if (!live) {
            lv_obj_add_flag(s_seg_bg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_seg_label[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_seg_bg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_seg_label[i], LV_OBJ_FLAG_HIDDEN);

        const auto& w = f.workers[i];
        const float start = i * span + kGapDeg / 2;
        const float full  = (i + 1) * span - kGapDeg / 2;
        const float used  = start + (full - start) * w.quota;

        ui::arc_set_range(s_seg_bg[i], start, full);
        ui::arc_animate_to(s_seg[i], start, used, T_FAST);
        ui::arc_set_color(s_seg[i], hue_of(w.state));

        lv_label_set_text(s_seg_label[i], w.label.c_str());
        lv_obj_set_style_text_color(s_seg_label[i],
                                    c(i == focus ? SILK : BRONZE_D), 0);
        ui::place_polar(s_seg_label[i], R_ORBIT_IN - 22, (start + full) / 2);

        // 额度低于一成时让这一段呼吸。这是唯一允许的"催促"
        if (w.quota < 0.10f && w.state != WorkerState::Down) {
            ui::breathe(s_seg[i], T_BREATH, 80, 255);
        } else {
            ui::stop_breathe(s_seg[i]);
        }
    }
}

void redraw()
{
    const auto s = State::get().snapshot();
    const auto& f = s.fleet;

    rebuild(f);

    const bool stale = f.stale || (GetHAL().millis() - f.last_recv > STALE_MS);
    ui::arc_set_color(s_rim, stale ? INDIGO : MALACHITE);

    if (f.count == 0) {
        // 空态是邀请，不是错误
        ui::numeral_set(s_focus_pct, "--");
        lv_label_set_text(s_focus_name, "no fleet");
        lv_label_set_text(s_chord, stale ? "daemon offline" : "waiting for daemon");
        return;
    }

    int focus = shell::work_focus();
    if (focus >= f.count) {
        focus = 0;
        shell::set_work_focus(0);
    }
    const auto& w = f.workers[focus];

    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d", static_cast<int>(w.quota * 100 + 0.5f));
    ui::numeral_set(s_focus_pct, pct);
    lv_obj_set_style_text_color(s_focus_pct, c(hue_of(w.state)), 0);

    lv_label_set_text(s_focus_name, w.label.c_str());

    char line[64];
    std::snprintf(line, sizeof(line), "%s  %s", name_of(w.state),
                  w.task.empty() ? "-" : w.task.c_str());
    lv_label_set_text(s_chord, line);
    lv_obj_set_style_text_color(s_chord, c(stale ? INDIGO : BRONZE_D), 0);
}

void cycle_focus(int dir)
{
    const auto s = State::get().snapshot();
    if (s.fleet.count <= 0) return;
    shell::set_work_focus((shell::work_focus() + dir + s.fleet.count) % s.fleet.count);
    haptics::play(haptics::Cue::Tick);
    redraw();
}

// A 短：对 focus 发 HID OK。没有待批事项时这是一发「继续」，
// 有 Buddy prompt 时 shell 已经进了 Interrupt，到不了这里
void send_ok()
{
    if (hid::send(hid::Action::Ok)) {
        haptics::play(haptics::Cue::Ok);
        lv_label_set_text(s_chord, "ok → enter");
        lv_obj_set_style_text_color(s_chord, c(MALACHITE), 0);
    } else {
        // 不弹窗。弦区一行小字说明为什么没反应
        haptics::vibe_only(haptics::Cue::Soft);
        lv_label_set_text(s_chord, "no hid · pair in bluetooth settings");
        lv_obj_set_style_text_color(s_chord, c(INDIGO), 0);
    }
}

}  // namespace

void create()
{
    LvglLockGuard lock;
    s_stage = ui::stage(ui::precession_root());

    // Rim 只做一件事：数据新鲜度。陈旧就整圈转靛青
    s_rim = ui::arc(s_stage, {R_RIM, 4, MALACHITE, INK, true}, 0, 359.9f);

    for (int i = 0; i < kMaxSeg; i++) {
        s_seg_bg[i]    = ui::arc(s_stage, {R_ORBIT, W_ORBIT + 2, LACQUER, INK, true}, 0, 1);
        s_seg[i]       = ui::arc(s_stage, {R_ORBIT, W_ORBIT, INDIGO, INK, true}, 0, 1);
        s_seg_label[i] = ui::text(s_stage, "", SN_FONT_MONO_S, BRONZE_D);
        lv_obj_add_flag(s_seg_bg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_seg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_seg_label[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_focus_pct = ui::numeral(s_stage, "--");
    lv_obj_align(s_focus_pct, LV_ALIGN_CENTER, 0, -24);

    s_focus_name = ui::text(s_stage, "", SN_FONT_MONO_M, BRONZE);
    lv_obj_align(s_focus_name, LV_ALIGN_CENTER, 0, 52);

    s_chord = ui::chord(s_stage, "B cycle · A ok");

    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    mclog::tagInfo("Work", "created");
}

void enter()
{
    LvglLockGuard lock;
    lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    ui::set_luma(ui::Luma::Normal);
    redraw();
}

void exit()
{
    LvglLockGuard lock;
    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
}

void tick(uint32_t now)
{
    if (now - s_last_draw > 1000) {
        s_last_draw = now;
        LvglLockGuard lock;
        redraw();
    }
}

bool key(shell::Key k)
{
    LvglLockGuard lock;
    switch (k) {
        case shell::Key::BShort: cycle_focus(1);  return true;
        case shell::Key::AShort: send_ok();       return true;
        default: return false;
    }
}

void rim_slide(float delta_deg, bool, uint32_t)
{
    // 阵上滑动 = 拨 agent 环，快慢同义
    static float acc = 0.0f;
    acc += delta_deg;
    if (acc > 30.0f)  { acc = 0.0f; LvglLockGuard lock; cycle_focus(1); }
    if (acc < -30.0f) { acc = 0.0f; LvglLockGuard lock; cycle_focus(-1); }
}

void tap_center(uint32_t)
{
    // 点某一段 = 选中那个 agent；点在段外 = 无操作
    const auto tp = GetHAL().getTouchPoint();
    if (tp.num <= 0) return;

    const float dx = static_cast<float>(tp.x - CENTER);
    const float dy = static_cast<float>(tp.y - CENTER);
    const float r = std::sqrt(dx * dx + dy * dy);
    if (r < R_ORBIT - 40 || r > R_ORBIT + 40) return;

    float deg = std::atan2(dx, -dy) * 180.0f / 3.14159265358979f;  // 12 点为 0°，顺时针
    if (deg < 0) deg += 360.0f;

    const auto s = State::get().snapshot();
    if (s.fleet.count <= 0) return;
    const int idx = static_cast<int>(deg / (360.0f / s.fleet.count));
    if (idx < s.fleet.count) {
        shell::set_work_focus(idx);
        haptics::play(haptics::Cue::Tick);
        LvglLockGuard lock;
        redraw();
    }
}

}  // namespace sinan::layer_work

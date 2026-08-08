/*
 * app_fleet.cpp — Work：三个真实编程工具的状态与语音入口。
 *
 * 首页 B 切换 Codex / PASEO / Grok Build，A 进入。工具内 A 开始或
 * 结束录音，转写显示在屏上后由 B 发送，避免把半句话直接交给编程代理。
 */
#include "app_fleet.h"
#include <sinan/bridge_ws.h>
#include <sinan/design.h>
#include <sinan/precession.h>
#include <sinan/ring.h>
#include <sinan/route.h>
#include <sinan/state.h>
#include <sinan/voice.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

constexpr float kGapDeg = 8.0f;
constexpr uint32_t kMaxRecMs = 20000;

struct Target {
    const char* id;
    const char* label;
};

constexpr Target kTargets[] = {
    {"codex", "CODEX"},
    {"paseo", "PASEO"},
    {"grok", "GROK BUILD"},
};

constexpr const char* kTargetMarks[] = {"C", "P", "G"};

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
        case WorkerState::Idle:  return "ready";
        case WorkerState::Stall: return "attention";
        default:                 return "offline";
    }
}

Worker find_worker(const FleetState& f, const char* id)
{
    for (int i = 0; i < f.count; ++i) {
        if (f.workers[i].id == id) return f.workers[i];
    }
    Worker out;
    out.id = id;
    out.label = id;
    return out;
}

void set_hidden(lv_obj_t* obj, bool hidden)
{
    if (!obj) return;
    if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace

AppFleet::AppFleet()
{
    static uint32_t launcher_color = MALACHITE;
    setAppInfo().name = "Work";
    setAppInfo().icon = (void*)&icon_imu;
    setAppInfo().userData = &launcher_color;
}

void AppFleet::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

const char* AppFleet::target_id() const
{
    return kTargets[_focus % 3].id;
}

const char* AppFleet::target_label() const
{
    return kTargets[_focus % 3].label;
}

void AppFleet::onOpen()
{
    _key = std::make_unique<input::KeyManager>(true);
    _focus = 0;
    _session = false;
    _spin = 0.0f;

    LvglLockGuard lock;
    ui::root_acquire();
    _stage = ui::stage(ui::precession_root());
    _rim = ui::arc(_stage, {R_RIM, 4, MALACHITE, INK, true}, 0, 359.9f);

    for (int i = 0; i < kMaxSeg; ++i) {
        _seg_bg[i] = ui::arc(_stage, {R_ORBIT, W_ORBIT + 2, LACQUER, INK, true}, 0, 1);
        _seg[i] = ui::arc(_stage, {R_ORBIT, W_ORBIT, INDIGO, INK, true}, 0, 1);
        _seg_mark[i] = ui::text(_stage, kTargetMarks[i], SN_FONT_MONO_M, BRONZE);
        _seg_label[i] = ui::text(_stage, kTargets[i].label, SN_FONT_MONO_S, BRONZE_D);
    }

    _focus_pct = ui::numeral(_stage, "--");
    lv_obj_align(_focus_pct, LV_ALIGN_CENTER, 0, -30);
    _focus_name = ui::text(_stage, "", SN_FONT_MONO_M, BRONZE);
    lv_obj_align(_focus_name, LV_ALIGN_CENTER, 0, 55);
    _body = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK, 292);
    lv_obj_align(_body, LV_ALIGN_CENTER, 0, 102);
    _chord = ui::chord(_stage, "A open / B next");
    ui::set_luma(ui::Luma::Normal);
    redraw();
}

void AppFleet::rebuild(const FleetState& f)
{
    constexpr float span = 120.0f;
    for (int i = 0; i < kMaxSeg; ++i) {
        set_hidden(_seg_bg[i], false);
        set_hidden(_seg[i], false);
        set_hidden(_seg_mark[i], false);
        set_hidden(_seg_label[i], false);
        const auto w = find_worker(f, kTargets[i].id);
        const float start = i * span + kGapDeg / 2;
        const float full = (i + 1) * span - kGapDeg / 2;
        const float end = start + (full - start) * std::clamp(w.quota, 0.0f, 1.0f);
        ui::arc_set_range(_seg_bg[i], start, full);
        ui::arc_animate_to(_seg[i], start, end, T_FAST);
        ui::arc_set_color(_seg[i], hue_of(w.state));
        lv_label_set_text(_seg_label[i], kTargets[i].label);
        lv_obj_set_style_text_color(_seg_label[i], c(i == _focus ? SILK : BRONZE_D), 0);
        lv_obj_set_style_text_color(_seg_mark[i], c(i == _focus ? hue_of(w.state) : BRONZE_D), 0);
        ui::place_polar(_seg_mark[i], R_ORBIT_IN - 48, (start + full) / 2);
        ui::place_polar(_seg_label[i], R_ORBIT_IN - 22, (start + full) / 2);

        const bool low = w.quota < 0.10f && w.state != WorkerState::Down;
        if (low != _breathing[i]) {
            _breathing[i] = low;
            if (low) ui::breathe(_seg[i], T_BREATH, 80, 255);
            else ui::stop_breathe(_seg[i]);
        }
    }
}

void AppFleet::redraw_selector(const FleetState& f, bool stale)
{
    rebuild(f);
    set_hidden(_focus_pct, false);
    set_hidden(_focus_name, false);
    set_hidden(_body, false);
    lv_obj_align(_focus_pct, LV_ALIGN_CENTER, 0, -30);
    lv_obj_align(_focus_name, LV_ALIGN_CENTER, 0, 55);
    lv_obj_align(_body, LV_ALIGN_CENTER, 0, 102);

    const auto w = find_worker(f, target_id());
    char pct[8];
    if (w.state == WorkerState::Down) std::snprintf(pct, sizeof(pct), "--");
    else std::snprintf(pct, sizeof(pct), "%d", static_cast<int>(w.quota * 100 + 0.5f));
    ui::numeral_set(_focus_pct, pct);
    lv_obj_set_style_text_color(_focus_pct, c(hue_of(w.state)), 0);
    lv_label_set_text(_focus_name, target_label());

    char status[72];
    std::snprintf(status, sizeof(status), "%s%s%s", name_of(w.state),
                  w.task.empty() ? "" : "  ", w.task.empty() ? "" : w.task.c_str());
    lv_label_set_text(_body, status);
    lv_label_set_text(_chord, "A open / B next");
    lv_obj_set_style_text_color(_chord, c(stale ? INDIGO : BRONZE_D), 0);
    ui::arc_set_range(_rim, 0, 359.9f);
    ui::arc_set_color(_rim, stale ? INDIGO : MALACHITE);
}

void AppFleet::redraw_session()
{
    for (int i = 0; i < kMaxSeg; ++i) {
        set_hidden(_seg_bg[i], true);
        set_hidden(_seg[i], true);
        set_hidden(_seg_mark[i], true);
        set_hidden(_seg_label[i], true);
    }
    set_hidden(_focus_pct, true);
    set_hidden(_focus_name, false);
    set_hidden(_body, false);
    lv_obj_align(_focus_name, LV_ALIGN_CENTER, 0, -118);
    lv_obj_align(_body, LV_ALIGN_CENTER, 0, 12);
    lv_label_set_text(_focus_name, target_label());

    const auto s = State::get().snapshot();
    const auto& v = s.voice;
    const uint32_t now = GetHAL().millis();
    const bool same_target = v.target.empty() || v.target == target_id();

    switch (v.phase) {
        case VoicePhase::Recording: {
            lv_label_set_text(_body, "LISTENING");
            lv_label_set_text(_chord, "A stop");
            ui::arc_set_color(_rim, MALACHITE);
            const float left = 1.0f - std::min(1.0f, (now - _rec_start) / static_cast<float>(kMaxRecMs));
            ui::arc_set_progress(_rim, left);
            break;
        }
        case VoicePhase::Sending:
        case VoicePhase::Transcribing:
            _spin += 8.0f;
            if (_spin >= 360.0f) _spin -= 360.0f;
            ui::arc_set_range(_rim, _spin, _spin + 54.0f);
            ui::arc_set_color(_rim, AMBER);
            lv_label_set_text(_body, v.heard.empty() ? "TRANSCRIBING" : v.heard.c_str());
            lv_label_set_text(_chord, v.heard.empty() ? "please wait" : "sending");
            break;
        case VoicePhase::Ready:
            ui::arc_set_range(_rim, 0, 359.9f);
            ui::arc_set_color(_rim, BRONZE);
            lv_label_set_text(_body, same_target ? v.heard.c_str() : "target changed");
            lv_label_set_text(_chord, same_target ? "A redo / B send" : "A record again");
            break;
        case VoicePhase::Thinking:
            _spin += 5.0f;
            if (_spin >= 360.0f) _spin -= 360.0f;
            ui::arc_set_range(_rim, _spin, _spin + 72.0f);
            ui::arc_set_color(_rim, MALACHITE);
            lv_label_set_text(_body, v.heard.c_str());
            lv_label_set_text(_chord, "running");
            break;
        case VoicePhase::Speaking:
            ui::arc_set_range(_rim, 0, 359.9f);
            ui::arc_set_color(_rim, SILK);
            lv_label_set_text(_body, v.reply.c_str());
            lv_label_set_text(_chord, "A new instruction");
            break;
        case VoicePhase::Error:
            ui::arc_set_range(_rim, 0, 46.0f);
            ui::arc_set_color(_rim, INDIGO);
            lv_label_set_text(_body, v.note.c_str());
            lv_label_set_text(_chord, "A retry");
            break;
        case VoicePhase::Idle:
        default:
            ui::arc_set_range(_rim, 0, 359.9f);
            ui::arc_set_color(_rim, s.ws_up ? BRONZE : INDIGO);
            lv_label_set_text(_body, v.reply.empty() ? "VOICE INPUT" : v.reply.c_str());
            lv_label_set_text(_chord, s.ws_up ? "A record / B sends" : "Mac bridge offline");
            break;
    }
}

void AppFleet::redraw()
{
    const auto s = State::get().snapshot();
    const bool stale = s.fleet.stale || (GetHAL().millis() - s.fleet.last_recv > STALE_MS);
    if (_session) redraw_session();
    else redraw_selector(s.fleet, stale);
}

void AppFleet::onRunning()
{
    auto& hal = GetHAL();
    hal.updateButtonStates();
    const auto event = _key ? _key->update(false) : input::KeyEvent::None;

    if (event == input::KeyEvent::GoHome) {
        if (_session) {
            if (voice::busy()) voice::abort();
            voice::clear();
            _session = false;
            hal.vibrate(35, 55);
            LvglLockGuard lock;
            redraw();
        } else {
            close();
        }
        return;
    }
    if (!voice::busy() && sinan::route::yield_to_ward_if_needed()) return;

    bool dirty = false;
    if (!_session) {
        if (event == input::KeyEvent::GoPrevious) {
            voice::clear();
            _session = true;
            hal.vibrate(40, 60);
            dirty = true;
        } else if (event == input::KeyEvent::GoNext) {
            _focus = (_focus + 1) % 3;
            hal.vibrate(30, 50);
            dirty = true;
        }
    } else {
        const auto s = State::get().snapshot();
        if (event == input::KeyEvent::GoPrevious) {
            if (s.voice.phase == VoicePhase::Recording) {
                voice::end();
            } else if (s.voice.phase != VoicePhase::Sending &&
                       s.voice.phase != VoicePhase::Transcribing &&
                       s.voice.phase != VoicePhase::Thinking && !voice::busy()) {
                voice::clear();
                _rec_start = hal.millis();
                voice::begin(target_id());
            }
            dirty = true;
        } else if (event == input::KeyEvent::GoNext && s.voice.phase == VoicePhase::Ready) {
            if (voice::submit(target_id())) hal.vibrate(55, 70);
            dirty = true;
        }

        if (s.voice.phase == VoicePhase::Recording && hal.millis() - _rec_start >= kMaxRecMs) {
            voice::end();
            dirty = true;
        }
    }

    const uint32_t interval = _session ? 120 : 1000;
    if (dirty || hal.millis() - _last_draw >= interval) {
        _last_draw = hal.millis();
        LvglLockGuard lock;
        redraw();
    }
}

void AppFleet::onClose()
{
    if (voice::busy()) voice::abort();
    voice::clear();
    _session = false;
    _key.reset();
    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _focus_pct = _focus_name = _body = _chord = nullptr;
    _seg.fill(nullptr);
    _seg_bg.fill(nullptr);
    _seg_mark.fill(nullptr);
    _seg_label.fill(nullptr);
    _breathing.fill(false);
    ui::set_luma(ui::Luma::Normal);
}

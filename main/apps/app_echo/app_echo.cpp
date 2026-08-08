/*
 * app_echo.cpp — 问。
 *
 * 电平环用的是 HAL 白送的 20 段频谱，沿圆周每段 18°。
 * 参考方案里麦克风完全没接，这块板一半的表现力就浪费在那儿。
 */
#include "app_echo.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/state.h>
#include <sinan/route.h>
#include <sinan/voice.h>
#include <sinan/bridge_ws.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {
constexpr uint32_t kMaxRecMs  = 12000;   // 说太久转写会慢到不像话
constexpr uint32_t kWaitMaxMs = 120000;  // 等结果的上限。CLI 跑长任务是常事
constexpr int kBarMin = 6;
constexpr int kBarMax = 46;

// 画面形态。跟 VoicePhase 不是一一对应 —— UI 只关心"看起来该是哪样"
enum Face { FaceIdle = 0, FaceRec, FaceWait, FaceSpeak, FaceError };
}  // namespace

AppEcho::AppEcho()
{
    static uint32_t launcher_color = BRONZE;
    setAppInfo().name = "Voice";
    setAppInfo().icon = (void*)&icon_fft;
    setAppInfo().userData = &launcher_color;
}

void AppEcho::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

void AppEcho::onOpen()
{
    _key = std::make_unique<input::KeyManager>(true);
    _recording = false;

    LvglLockGuard lock;
    ui::root_acquire();   // 与 onClose 的 root_release 成对，漏了会全屏黑

    _stage = ui::stage(ui::precession_root());
    _rim   = ui::arc(_stage, {R_RIM, 4, INDIGO, INK, true}, 0, 359.9f);

    for (int i = 0; i < kBands; i++) {
        lv_obj_t* b = lv_obj_create(_stage);
        lv_obj_set_size(b, 5, kBarMin);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_set_style_bg_color(b, c(BRONZE_D), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        const float deg = 360.0f * i / kBands;
        lv_obj_set_style_transform_rotation(b, static_cast<int32_t>(deg * 10), 0);
        lv_obj_set_style_transform_pivot_x(b, 2, 0);
        lv_obj_set_style_transform_pivot_y(b, kBarMin / 2, 0);
        ui::place_polar(b, R_ORBIT, deg);
        _bars[i] = b;
    }

    // 静默时中心是一个小圆环，像一只闭着的耳朵
    _glyph = ui::arc(_stage, {58, 3, BRONZE_D, INK, true}, 0, 359.9f);
    _text  = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK, 250);
    lv_obj_align(_text, LV_ALIGN_CENTER, 0, 0);

    _chord = ui::chord(_stage, "");
    _face = -1;
    set_face(FaceIdle, ws::connected() ? "hold A to ask" : "hold A / local playback", BRONZE_D);
}

/*
 * 只在形态真的变了的时候重画。弦区那行字每帧都可能不同（倒计时），
 * 所以 line 单独更新，不触发整套重建
 */
void AppEcho::set_face(int f, const char* line, uint32_t hue)
{
    if (line) {
        lv_label_set_text(_chord, line);
        lv_obj_set_style_text_color(_chord, c(hue), 0);
    }
    if (f == _face) return;
    _face = f;

    switch (f) {
        case FaceIdle:
            ui::set_luma(ui::Luma::Quiet);
            ui::arc_set_color(_rim, INDIGO);
            ui::arc_set_range(_rim, 0, 359.9f);
            ui::breathe(_rim, T_BREATH * 2, 30, 120);
            lv_obj_clear_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_text, "");
            break;
        case FaceRec:
            ui::set_luma(ui::Luma::Alert);
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, MALACHITE);
            lv_obj_add_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_text, "");
            GetHAL().vibrate(50, 80);
            break;
        case FaceWait:
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, AMBER);
            lv_obj_clear_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            // 松手的这一下要有反馈 —— 用户需要知道"已经发出去了"
            GetHAL().vibrate(40, 70);
            break;
        case FaceSpeak:
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, SILK);
            ui::arc_set_range(_rim, 0, 359.9f);
            break;
        case FaceError:
            // 靛青而不是朱砂：没链路、没听清都不是危险，是"什么都没发生"。
            // 朱砂只留给守那一页的不可逆操作 —— 用滥了它就吓不住人了
            ui::stop_breathe(_rim);
            ui::arc_set_color(_rim, INDIGO);
            ui::arc_set_range(_rim, 0, 359.9f);
            lv_obj_clear_flag(_glyph, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(_text, "");
            GetHAL().vibrate(120, 80);
            break;
        default:
            break;
    }
}

void AppEcho::draw_levels()
{
    auto& hal = GetHAL();
    hal.updateAudioSpectrum();
    const auto& f = hal.getAudioSpectrum();

    for (int i = 0; i < kBands; i++) {
        const float v = std::clamp(f.bands[i], 0.0f, 1.0f);
        const int h = kBarMin + static_cast<int>((kBarMax - kBarMin) * v);
        lv_obj_set_height(_bars[i], h);
        lv_obj_set_style_transform_pivot_y(_bars[i], h / 2, 0);
        // 从暗鎏金推向石绿，越响越亮。颜色只有一条轴，不搞彩虹
        lv_obj_set_style_bg_color(_bars[i], c(v > 0.55f ? MALACHITE : BRONZE), 0);
        ui::place_polar(_bars[i], R_ORBIT, 360.0f * i / kBands);
    }
}

void AppEcho::onRunning()
{
    auto& hal = GetHAL();
    hal.updateButtonStates();
    if (_key && _key->update(false) == input::KeyEvent::GoHome) {
        if (_recording) voice::abort();
        close();
        return;
    }
    if (!_recording && sinan::route::yield_to_ward_if_needed()) return;

    const uint32_t now = hal.millis();
    const auto s = State::get().snapshot();

    LvglLockGuard lock;

    /* ---------- 录音中 ---------- */
    if (!_recording && hal.btnA.isPressed() && !voice::busy()) {
        _recording = true;
        _rec_start = now;
        voice::begin("codex");
        set_face(FaceRec, "listening", MALACHITE);
    }

    if (_recording) {
        // begin() 可能因为没链路直接判错，这时不该继续画录音态
        if (s.voice.phase == VoicePhase::Error) {
            _recording = false;
            set_face(FaceError, s.voice.note.c_str(), INDIGO);
            _wait_start = now;
            return;
        }
        draw_levels();
        const uint32_t used = now - _rec_start;
        ui::arc_set_progress(_rim, 1.0f - std::min(1.0f, static_cast<float>(used) / kMaxRecMs));

        // 松开即发。没有第二步确认 —— 说完话还要再按一下，这功能就没人用了
        if (!hal.btnA.isPressed() || used > kMaxRecMs) {
            _recording  = false;
            _wait_start = now;
            voice::end();
            set_face(FaceWait, "sent", AMBER);
        }
        return;
    }

    /* ---------- 等结果 ---------- */
    if (_face == FaceWait || _face == FaceSpeak) {
        // B 键随时复位，不必等超时
        if (hal.btnB.wasClicked()) {
            set_face(FaceIdle, ws::connected() ? "hold A to ask" : "hold A / local playback", BRONZE_D);
            ui::set_luma(ui::Luma::Normal);
            return;
        }
        _spin += 3.0f;
        if (_spin >= 360.0f) _spin -= 360.0f;

        switch (s.voice.phase) {
            case VoicePhase::Sending:
                ui::arc_set_range(_rim, _spin, _spin + 50.0f);
                set_face(FaceWait, "sent", AMBER);
                break;
            case VoicePhase::Transcribing:
                ui::arc_set_range(_rim, _spin, _spin + 50.0f);
                set_face(FaceWait, "transcribing", AMBER);
                break;
            case VoicePhase::Thinking:
                ui::arc_set_range(_rim, _spin, _spin + 50.0f);
                if (!s.voice.heard.empty()) lv_label_set_text(_text, s.voice.heard.c_str());
                set_face(FaceWait, "running", AMBER);
                break;
            case VoicePhase::Speaking:
                lv_label_set_text(_text, s.voice.reply.c_str());
                set_face(FaceSpeak, "", SILK);
                break;
            case VoicePhase::Error:
                set_face(FaceError, s.voice.note.c_str(), INDIGO);
                _wait_start = now;
                break;
            case VoicePhase::Idle:
                if (!s.voice.reply.empty()) {
                    lv_label_set_text(_text, s.voice.reply.c_str());
                    set_face(FaceSpeak, ws::connected() ? "hold A to ask" : "hold A / local playback", BRONZE_D);
                    ui::set_luma(ui::Luma::Normal);
                    // 已经播完了，退出等待计时。不清零的话 120 秒后
                    // 一次成功的对话会被超时判定改写成 timed out
                    _wait_start = 0;
                }
                break;
            default:
                break;
        }

        if (_wait_start && now - _wait_start > kWaitMaxMs) {
            set_face(FaceError, "timed out", INDIGO);
            _wait_start = now;
        }
        return;
    }

    /* ---------- 错误：停留几秒自己退回 ---------- */
    if (_face == FaceError) {
        if (now - _wait_start > 4000 || hal.btnB.wasClicked()) {
            set_face(FaceIdle, ws::connected() ? "hold A to ask" : "hold A / local playback", BRONZE_D);
            ui::set_luma(ui::Luma::Normal);
        }
        return;
    }
}

void AppEcho::onClose()
{
    if (_recording) voice::abort();
    _recording = false;
    _key.reset();

    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _glyph = _text = _chord = nullptr;
    _bars.fill(nullptr);
    ui::set_luma(ui::Luma::Normal);
}

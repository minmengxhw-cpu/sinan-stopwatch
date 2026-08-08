#include "app_wenwan.h"

#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <sinan/design.h>
#include <sinan/precession.h>
#include <sinan/ring.h>
#include <algorithm>
#include <cstdio>

using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

constexpr int kScentCount = 3;
constexpr const char* kScentProfiles[kScentCount] = {
    "HAINAN / SOFT",
    "HUIAN / DEEP",
    "VIETNAM / RESIN",
};

constexpr uint32_t kLightingMs = 3500;
constexpr uint32_t kBurningMs = 12000;
constexpr uint32_t kAfterglowMs = 6500;

}  // namespace

AppWenwan::AppWenwan()
{
    static uint32_t launcher_color = BRONZE;
    setAppInfo().name = "Agarwood";
    setAppInfo().icon = (void*)&icon_incense;
    setAppInfo().userData = &launcher_color;
}

void AppWenwan::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create, agarwood ritual ready");
}

void AppWenwan::onOpen()
{
    _key = std::make_unique<input::KeyManager>(true);
    _phase = Phase::Select;
    _scent = 0;
    _phase_started = GetHAL().millis();
    _last_draw = 0;

    LvglLockGuard lock;
    ui::root_acquire();
    _stage = ui::stage(ui::precession_root());
    _rim = ui::arc(_stage, {R_RIM, W_RIM, BRONZE, LACQUER, true}, 0, 320);
    _orbit = ui::ticks(_stage, R_ORBIT, 24, 14, 3, BRONZE_D);
    _progress = ui::arc(_stage, {R_CORE, 8, AMBER, INK, true}, 0, 1);

    // A real stick is the centrepiece: holder, stick, tip ember, flame and
    // rising smoke. The phase animation makes the ritual legible at a glance.
    _holder = lv_obj_create(_stage);
    lv_obj_set_size(_holder, 48, 16);
    lv_obj_set_style_radius(_holder, 8, 0);
    lv_obj_set_style_bg_color(_holder, c(BRONZE_D), 0);
    lv_obj_set_style_border_color(_holder, c(BRONZE), 0);
    lv_obj_set_style_border_width(_holder, 2, 0);
    lv_obj_align(_holder, LV_ALIGN_CENTER, 0, 96);
    lv_obj_remove_flag(_holder, LV_OBJ_FLAG_SCROLLABLE);

    _stick = lv_obj_create(_stage);
    lv_obj_set_size(_stick, 9, 124);
    lv_obj_set_style_radius(_stick, 4, 0);
    lv_obj_set_style_bg_color(_stick, c(BRONZE), 0);
    lv_obj_set_style_border_color(_stick, c(SILK_D), 0);
    lv_obj_set_style_border_width(_stick, 1, 0);
    lv_obj_align(_stick, LV_ALIGN_CENTER, 0, 24);
    lv_obj_remove_flag(_stick, LV_OBJ_FLAG_SCROLLABLE);

    _flame = lv_obj_create(_stage);
    lv_obj_set_size(_flame, 22, 34);
    lv_obj_set_style_radius(_flame, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_flame, c(AMBER), 0);
    lv_obj_set_style_border_color(_flame, c(BRONZE), 0);
    lv_obj_set_style_border_width(_flame, 2, 0);
    lv_obj_align(_flame, LV_ALIGN_CENTER, 0, -55);
    lv_obj_remove_flag(_flame, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < static_cast<int>(_smoke.size()); ++i) {
        _smoke[i] = lv_obj_create(_stage);
        lv_obj_set_size(_smoke[i], 16, 16);
        lv_obj_set_style_radius(_smoke[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(_smoke[i], c(SILK_D), 0);
        lv_obj_set_style_border_width(_smoke[i], 0, 0);
        lv_obj_remove_flag(_smoke[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    _ember = lv_obj_create(_stage);
    lv_obj_set_size(_ember, 16, 16);
    lv_obj_set_style_radius(_ember, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_ember, c(AMBER), 0);
    lv_obj_set_style_border_color(_ember, c(BRONZE), 0);
    lv_obj_set_style_border_width(_ember, 2, 0);
    lv_obj_align(_ember, LV_ALIGN_CENTER, 0, -43);
    lv_obj_remove_flag(_ember, LV_OBJ_FLAG_SCROLLABLE);

    _title = ui::text(_stage, "AGARWOOD", SN_FONT_MONO_L, BRONZE);
    lv_obj_align(_title, LV_ALIGN_CENTER, 0, -76);
    _phase_label = ui::text(_stage, "SELECT", SN_FONT_MONO_M, SILK);
    lv_obj_align(_phase_label, LV_ALIGN_CENTER, 0, -30);
    _detail = ui::text(_stage, kScentProfiles[0], SN_FONT_MONO_S, SILK_D);
    lv_obj_align(_detail, LV_ALIGN_CENTER, 0, 52);
    _chord = ui::chord(_stage, "A choose  /  B ignite", BRONZE_D);
    ui::set_luma(ui::Luma::Normal);
    ui::breathe(_ember, T_BREATH * 2, 90, 210);
    ui::breathe(_flame, T_BREATH, 80, 220);
    redraw();
}

uint32_t AppWenwan::phase_duration() const
{
    switch (_phase) {
        case Phase::Lighting: return kLightingMs;
        case Phase::Burning: return kBurningMs;
        case Phase::Afterglow: return kAfterglowMs;
        case Phase::Select:
        default: return 1;
    }
}

float AppWenwan::phase_ratio(uint32_t now) const
{
    if (_phase == Phase::Select) return 0.0f;
    const uint32_t elapsed = now - _phase_started;
    return std::clamp(static_cast<float>(elapsed) / phase_duration(), 0.0f, 1.0f);
}

void AppWenwan::redraw()
{
    if (!_stage) return;

    const uint32_t now = GetHAL().millis();
    const float ratio = phase_ratio(now);
    const char* phase = "SELECT";
    const char* detail = kScentProfiles[_scent];
    const char* chord = "A choose  /  B ignite";
    uint32_t hue = BRONZE;

    switch (_phase) {
        case Phase::Lighting:
            phase = "LIGHT";
            detail = "bring flame close / wait for ember";
            chord = "B next  /  A cancel";
            hue = AMBER;
            break;
        case Phase::Burning:
            phase = "BURN";
            detail = "first smoke rises / breathe slowly";
            chord = "B next  /  A extinguish";
            hue = BRONZE;
            break;
        case Phase::Afterglow:
            phase = "AFTERGLOW";
            detail = "the fragrance remains / stay a moment";
            chord = "B new incense  /  A close";
            hue = INDIGO;
            break;
        case Phase::Select:
        default:
            break;
    }

    lv_label_set_text(_phase_label, phase);
    lv_label_set_text(_detail, detail);
    lv_label_set_text(_chord, chord);
    lv_obj_set_style_text_color(_phase_label, c(hue == INDIGO ? SILK : hue), 0);
    lv_obj_set_style_text_color(_detail, c(hue == AMBER ? BRONZE : SILK_D), 0);
    ui::arc_set_color(_rim, hue == INDIGO ? BRONZE_D : hue);
    ui::arc_set_color(_progress, hue);
    ui::arc_set_progress(_progress, ratio);
    const bool lighting = _phase == Phase::Lighting;
    const bool smoke_visible = _phase == Phase::Lighting || _phase == Phase::Burning || _phase == Phase::Afterglow;
    lv_obj_set_style_bg_color(_stick, c(_phase == Phase::Afterglow ? BRONZE_D : BRONZE), 0);
    lv_obj_set_style_bg_color(_holder, c(_phase == Phase::Afterglow ? LACQUER : BRONZE_D), 0);
    lv_obj_set_style_bg_color(_ember, c(hue == INDIGO ? BRONZE_D : AMBER), 0);
    lv_obj_set_style_border_color(_ember, c(hue == INDIGO ? SILK_D : BRONZE), 0);
    if (lighting) lv_obj_clear_flag(_flame, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_flame, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < static_cast<int>(_smoke.size()); ++i) {
        const int drift = static_cast<int>(ratio * 10.0f) + i * 3;
        lv_obj_align(_smoke[i], LV_ALIGN_CENTER, (i - 1) * 14, -82 - i * 26 - drift);
        if (smoke_visible) lv_obj_clear_flag(_smoke[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(_smoke[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(_smoke[i], smoke_visible ? static_cast<lv_opa_t>(190 - i * 45) : LV_OPA_TRANSP, 0);
    }
    lv_obj_set_style_opa(_flame, lighting ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    ui::ticks_reset_color(_orbit, LACQUER);
    const int lit = _phase == Phase::Select ? 3 : static_cast<int>(3 + ratio * 18.0f);
    for (int i = 0; i < lit && i < 24; ++i) {
        ui::tick_set_color(_orbit, (i * 24 / std::max(lit, 1)) % 24, hue == INDIGO ? BRONZE_D : hue);
    }
}

void AppWenwan::begin_ritual()
{
    _phase = Phase::Lighting;
    _phase_started = GetHAL().millis();
    GetHAL().vibrate(80, 70);
    redraw();
}

void AppWenwan::advance_phase()
{
    if (_phase == Phase::Select) {
        begin_ritual();
        return;
    }
    if (_phase == Phase::Lighting) _phase = Phase::Burning;
    else if (_phase == Phase::Burning) _phase = Phase::Afterglow;
    else {
        reset_ritual();
        return;
    }
    _phase_started = GetHAL().millis();
    GetHAL().vibrate(45, 55);
    redraw();
}

void AppWenwan::reset_ritual()
{
    _phase = Phase::Select;
    _phase_started = GetHAL().millis();
    redraw();
}

void AppWenwan::onRunning()
{
    auto& hal = GetHAL();
    hal.updateButtonStates();
    const auto event = _key ? _key->update(false) : input::KeyEvent::None;
    if (event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (_phase == Phase::Select) {
        if (event == input::KeyEvent::GoPrevious) {
            _scent = (_scent + kScentCount - 1) % kScentCount;
            hal.vibrate(25, 45);
            LvglLockGuard lock;
            redraw();
        } else if (event == input::KeyEvent::GoNext) {
            LvglLockGuard lock;
            begin_ritual();
        }
    } else if (event == input::KeyEvent::GoPrevious) {
        LvglLockGuard lock;
        reset_ritual();
    } else if (event == input::KeyEvent::GoNext) {
        LvglLockGuard lock;
        advance_phase();
    }

    const uint32_t now = hal.millis();
    if (_phase != Phase::Select && now - _phase_started >= phase_duration()) {
        LvglLockGuard lock;
        advance_phase();
    } else if (now - _last_draw >= 180) {
        _last_draw = now;
        LvglLockGuard lock;
        redraw();
    }
}

void AppWenwan::onClose()
{
    _key.reset();
    LvglLockGuard lock;
    if (_ember) lv_anim_delete(_ember, nullptr);
    if (_flame) lv_anim_delete(_flame, nullptr);
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _orbit = _progress = _stick = _holder = _flame = _ember = _title = _phase_label = _detail = _chord = nullptr;
    _smoke.fill(nullptr);
    ui::set_luma(ui::Luma::Normal);
}

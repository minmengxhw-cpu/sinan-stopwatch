/*
 * app_almanac.cpp — 历。
 *
 * 三个每天都会看一眼的盘，共用同一套环。切盘不是换页面，
 * 是同一块表盘换了一种读法 —— 所以过渡只改颜色和弧长，不重建对象。
 */
#include "app_almanac.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/state.h>
#include <sinan/route.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <ctime>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

constexpr int kDialCount  = 3;
constexpr int kWheelTicks = 73;  // 365 / 5，一格五天。365 个刻度在 466 屏上糊成一片

int day_of_year()
{
    const auto d = GetHAL().getDateYmd();
    int doy = d.day;
    for (int m = 1; m < d.month; m++) doy += DateYmd::daysInMonth(d.year, m);
    return doy;
}

}  // namespace

AppAlmanac::AppAlmanac()
{
    static uint32_t launcher_color = BRONZE;
    setAppInfo().name = "Today";
    setAppInfo().icon = (void*)&icon_lucky_wheel;
    setAppInfo().userData = &launcher_color;
}

void AppAlmanac::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

void AppAlmanac::onOpen()
{
    _key = std::make_unique<input::KeyManager>(true);
    _dial = 0;

    LvglLockGuard lock;
    ui::root_acquire();   // 与 onClose 的 root_release 成对，漏了会全屏黑

    _stage = ui::stage(ui::precession_root());

    _rim   = ui::arc(_stage, {R_RIM, W_RIM, BRONZE, LACQUER, true}, 0, 359.9f);
    _wheel = ui::ticks(_stage, R_ORBIT, kWheelTicks, 10, 2, LACQUER);
    lv_obj_add_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    _big = ui::numeral(_stage, "");
    lv_obj_align(_big, LV_ALIGN_CENTER, 0, -18);

    _sub = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK_D, 240);
    lv_obj_align(_sub, LV_ALIGN_CENTER, 0, 66);

    _chord = ui::chord(_stage, "A next dial");
    ui::set_luma(ui::Luma::Normal);
    redraw();
}

/* 号码盘：Rim 做一道缓慢扫描的短弧，中心是当日号码。
   这个盘的全部意义就是每天早上瞟一眼，所以中心只放一件东西。*/
void AppAlmanac::draw_number()
{
    const auto s = State::get().snapshot();
    lv_obj_add_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    ui::arc_set_color(_rim, MALACHITE);
    ui::arc_set_range(_rim, 0, 46);
    ui::breathe(_rim, T_BREATH * 2, 70, 255);

    const bool have = !s.almanac.number_code.empty();
    if (have) {
        ui::numeral_set(_big, s.almanac.number_code.c_str());
        lv_obj_set_style_text_color(_big, c(SILK), 0);
        lv_label_set_text(_sub, s.almanac.number_title.c_str());
        lv_label_set_text(_chord, "daily number / Mac");
    } else {
        const auto d = GetHAL().getDateYmd();
        char mmdd[8];
        char full[20];
        std::snprintf(mmdd, sizeof(mmdd), "%02u%02u", d.month, d.day);
        std::snprintf(full, sizeof(full), "%04u.%02u.%02u", d.year, d.month, d.day);
        ui::numeral_set(_big, mmdd);
        lv_obj_set_style_text_color(_big, c(SILK), 0);
        lv_label_set_text(_sub, "LOCAL DATE");
        lv_label_set_text(_chord, full);
    }
}

/* 黄历盘：HRV 相对基线做成一圈呼吸。高于基线是缓慢的石绿扩张，
   低于是急促的朱砂收缩 —— 呼吸的节奏本身就是读数，不需要写数字。*/
void AppAlmanac::draw_huangli()
{
    const auto s = State::get().snapshot();
    lv_obj_add_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    const bool have = !s.almanac.huangli_yi.empty() || !s.almanac.huangli_ji.empty();
    const std::string& t = s.almanac.huangli_trend;
    uint32_t hue = BRONZE;
    uint32_t period = T_BREATH * 2;
    if (t == "up")        { hue = MALACHITE; period = T_BREATH * 3; }
    // 琥珀而不是朱砂：状态偏低值得注意，但它不是危险
    else if (t == "down") { hue = AMBER; period = T_BREATH; }

    ui::arc_set_color(_rim, hue);
    ui::arc_set_range(_rim, 0, 359.9f);
    ui::breathe(_rim, period, 45, 220);

    if (have) {
        lv_label_set_text(_big, s.almanac.huangli_yi.c_str());
        lv_obj_set_style_text_font(_big, SN_FONT_CJK_L, 0);
        lv_obj_set_style_text_color(_big, c(SILK), 0);
        lv_label_set_text(_sub, s.almanac.huangli_ji.c_str());
        lv_obj_set_style_text_font(_sub, SN_FONT_CJK_M, 0);
        lv_label_set_text(_chord, "daily brief / Mac");
    } else {
        const auto now = GetHAL().getTimeHms();
        char clock[8];
        std::snprintf(clock, sizeof(clock), "%02u:%02u", now.hour, now.minute);
        ui::numeral_set(_big, clock);
        lv_obj_set_style_text_color(_big, c(SILK), 0);
        lv_label_set_text(_sub, "LOCAL CLOCK");
        lv_obj_set_style_text_font(_sub, SN_FONT_MONO_S, 0);
        lv_label_set_text(_chord, "Mac adds daily brief");
    }
}

/* 年轮盘：圆周即一年。今天所在的那一格点亮，
   已经过去的日子是暗鎏金，未来是漆色。一整年的进度是一个可以摸的实物。*/
void AppAlmanac::draw_yearring()
{
    const auto s = State::get().snapshot();
    lv_obj_clear_flag(_wheel, LV_OBJ_FLAG_HIDDEN);

    ui::stop_breathe(_rim);
    ui::arc_set_color(_rim, BRONZE_D);

    int doy = s.almanac.ring_doy > 0 ? s.almanac.ring_doy : day_of_year();
    if (doy < 1) doy = 1;
    if (doy > 366) doy = 366;
    // 用 doy - 1 映射到 0..kWheelTicks-1；否则闰年最后一天会落到环外。
    const int here = ((doy - 1) * kWheelTicks) / 366;

    ui::ticks_reset_color(_wheel, LACQUER);
    for (int i = 0; i < here; i++) ui::tick_set_color(_wheel, i, BRONZE_D);
    ui::tick_set_color(_wheel, here, SILK);

    ui::arc_set_progress(_rim, static_cast<float>(doy) / 366.0f);

    char d[12];
    std::snprintf(d, sizeof(d), "%d", doy);
    ui::numeral_set(_big, d);
    lv_obj_set_style_text_font(_big, SN_FONT_NUM_XL, 0);
    lv_obj_set_style_text_color(_big, c(SILK), 0);

    lv_label_set_text(_sub, s.almanac.ring_tag.c_str());
    lv_obj_set_style_text_font(_sub, SN_FONT_CJK_M, 0);

    // 只说今天和年进度。转动翻任意一天还没做，界面上就别吹
    char c1[32];
    std::snprintf(c1, sizeof(c1), "day %d of %d", doy, 366);
    lv_label_set_text(_chord, c1);
}

void AppAlmanac::redraw()
{
    // 每次切盘先归位字体，否则上一个盘的中文字体会漏到下一个盘
    lv_obj_set_style_text_font(_big, SN_FONT_NUM_XL, 0);
    lv_obj_set_style_text_font(_sub, SN_FONT_MONO_S, 0);
    lv_obj_set_style_text_color(_chord, c(BRONZE_D), 0);

    switch (_dial) {
        case 0: draw_number(); break;
        case 1: draw_huangli(); break;
        default: draw_yearring(); break;
    }
}

void AppAlmanac::onRunning()
{
    auto& hal = GetHAL();
    hal.updateButtonStates();
    const auto event = _key ? _key->update(false) : input::KeyEvent::None;
    if (event == input::KeyEvent::GoHome) {
        close();
        return;
    }

    if (sinan::route::yield_to_ward_if_needed()) return;

    bool dirty = false;
    if (event == input::KeyEvent::GoPrevious) {
        _dial = (_dial + 1) % kDialCount;
        hal.vibrate(40, 70);
        dirty = true;
    }

    if (dirty || hal.millis() - _last_draw > 5000) {
        _last_draw = hal.millis();
        LvglLockGuard lock;
        redraw();
    }
}

void AppAlmanac::onClose()
{
    _key.reset();
    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _wheel = _big = _sub = _chord = nullptr;
}

#include "app_tools.h"

#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <sinan/design.h>
#include <sinan/precession.h>
#include <sinan/ring.h>
#include <sinan/route.h>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

struct ToolEntry {
    const char* route;
    const char* title;
    const char* note;
    uint32_t hue;
};

constexpr ToolEntry kTools[] = {
    // Tools keeps the Mac bridge shortcut.  The top-level Connect app is the
    // separate local AP portal for changing Wi-Fi and WebSocket credentials.
    {"Connect", "MAC BRIDGE", "Codex / PASEO / Grok Build bridge", MALACHITE},
    {"Stopwatch", "STOPWATCH", "focus and speaking timer", MALACHITE},
    {"Badge", "BADGE", "meeting card or QR image", BRONZE},
    {"Settings", "SETTINGS", "screen sound buttons time", INDIGO},
};

constexpr int kToolCount = sizeof(kTools) / sizeof(kTools[0]);

}  // namespace

AppTools::AppTools()
{
    static uint32_t launcher_color = BRONZE;
    setAppInfo().name = "Tools";
    setAppInfo().icon = (void*)&icon_fft;
    setAppInfo().userData = &launcher_color;
}

void AppTools::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

void AppTools::onOpen()
{
    _key = std::make_unique<input::KeyManager>(true);
    _index = 0;

    LvglLockGuard lock;
    ui::root_acquire();
    _stage = ui::stage(ui::precession_root());
    _rim = ui::arc(_stage, {R_RIM, W_RIM, BRONZE, LACQUER, true}, 0, 90);
    _title = ui::text(_stage, "", SN_FONT_MONO_L, SILK);
    lv_obj_align(_title, LV_ALIGN_CENTER, 0, -38);
    _sub = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK_D, 260);
    lv_obj_align(_sub, LV_ALIGN_CENTER, 0, 42);
    _chord = ui::chord(_stage, "A open / B next");
    ui::set_luma(ui::Luma::Normal);
    redraw();
}

void AppTools::redraw()
{
    const auto& item = kTools[_index];
    lv_label_set_text(_title, item.title);
    lv_obj_set_style_text_color(_title, c(item.hue), 0);
    lv_label_set_text(_sub, item.note);
    ui::arc_set_color(_rim, item.hue);
    const float segment = 360.0f / static_cast<float>(kToolCount);
    ui::arc_animate_to(_rim, _index * segment, (_index + 1) * segment - 8.0f, T_FAST);
}

void AppTools::onRunning()
{
    auto& hal = GetHAL();
    hal.updateButtonStates();
    const auto event = _key ? _key->update(false) : input::KeyEvent::None;
    if (event == input::KeyEvent::GoHome) {
        close();
        return;
    }
    if (event == input::KeyEvent::GoNext) {
        _index = (_index + 1) % kToolCount;
        hal.vibrate(35, 55);
        LvglLockGuard lock;
        redraw();
        return;
    }
    if (event == input::KeyEvent::GoPrevious) {
        hal.vibrate(45, 65);
        sinan::route::open(kTools[_index].route);
    }
}

void AppTools::onClose()
{
    _key.reset();
    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _rim = _title = _sub = _chord = nullptr;
}

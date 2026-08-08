#include "app_connect.h"

#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <sinan/design.h>

using namespace mooncake;
using namespace sinan::design;

AppConnect::AppConnect()
{
    setAppInfo().name = "Connect";
    setAppInfo().icon = (void*)&icon_connect;
}

void AppConnect::onCreate() { mclog::tagInfo(getAppInfo().name, "on create"); }

void AppConnect::onOpen()
{
    _started = false;
    LvglLockGuard lock;
    _page = std::make_unique<view::LoadingPage>(INK, SILK);
    _page->setMessage("PRIVATE SETUP\nM5STOPWATCH-XXXX\nHTTP://192.168.4.1");
}

void AppConnect::onRunning()
{
    if (_started) return;
    _started = true;

    // The AP session intentionally owns this task until the user taps Done.
    // Release LVGL while the HTTP server waits so the status page can repaint.
    GetHAL().startBadgeEditModeViaAp([this](std::string_view message) {
        LvglLockGuard lock;
        if (_page) _page->setMessage(message);
    });
    close();
}

void AppConnect::onClose()
{
    LvglLockGuard lock;
    _page.reset();
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_launcher.h"
#include <apps/common/status_bar/status_bar.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <sinan/design.h>
#include <cstdint>
#include <algorithm>

using namespace mooncake;
using namespace sinan::design;

void AppLauncher::onLauncherCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    open();
}

void AppLauncher::onLauncherOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    show_guide_page();

    {
        LvglLockGuard lock;
        create_launcher_view();
        if (_is_first_open) {
            _pending_status_bar_create = true;
            _status_bar_create_tick    = GetHAL().millis() + 800;
        } else {
            view::create_status_bar(INK, BRONZE, true);
        }
    }

    if (_should_play_boot_sfx) {
        _should_play_boot_sfx = false;
        GetHAL().playBootSfx();
    }

    _last_charge_check_tick = GetHAL().millis();
    _was_battery_charging   = GetHAL().isBatteryCharging();

    _is_first_open = false;
}

void AppLauncher::onLauncherRunning()
{
    LvglLockGuard lock;

    uint32_t now = GetHAL().millis();

    // Check pending status bar creation
    if (_pending_status_bar_create && !view::is_status_bar_created() && now >= _status_bar_create_tick) {
        view::create_status_bar(INK, BRONZE, false);
        _pending_status_bar_create = false;
    }

    // Pop status bar if battery start charging
    if (now - _last_charge_check_tick >= 1000) {
        _last_charge_check_tick = now;

        bool is_battery_charging = GetHAL().isBatteryCharging();
        if (is_battery_charging && !_was_battery_charging && view::is_status_bar_created() &&
            view::is_status_bar_hidden()) {
            view::show_status_bar();
        }

        _was_battery_charging = is_battery_charging;
    }

    _view->update();
    view::update_status_bar();
}

void AppLauncher::onLauncherClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _pending_status_bar_create = false;
    _status_bar_create_tick    = 0;
    _last_charge_check_tick    = 0;
    _was_battery_charging      = false;
    _view.reset();
    view::destroy_status_bar();
}

void AppLauncher::onLauncherDestroy()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppLauncher::create_launcher_view()
{
    _view = std::make_unique<view::LauncherView>();
    auto props = getAppProps();
    // 照片第一；沉香仪式第二；其余按司南工作流排序。
    props.erase(std::remove_if(props.begin(), props.end(), [](const auto& item) {
        const auto& name = item.info.name;
        return name != "Photos" && name != "Agarwood" && name != "Work" && name != "Buddy" &&
               name != "Tools" && name != "Connect" && name != "Stopwatch" &&
               name != "Badge" && name != "Settings";
    }), props.end());
    const auto rank = [](const auto& item) {
        const auto& name = item.info.name;
        if (name == "Photos") return 0;
        if (name == "Agarwood") return 1;
        if (name == "Work") return 2;
        if (name == "Buddy") return 3;
        if (name == "Tools") return 4;
        if (name == "Connect") return 5;
        if (name == "Stopwatch") return 6;
        if (name == "Badge") return 7;
        return 8;
    };
    std::stable_sort(props.begin(), props.end(), [&](const auto& a, const auto& b) {
        return rank(a) < rank(b);
    });
    _view->init(std::move(props));
    _view->onAppClicked = [&](int appID) {
        mclog::tagInfo(getAppInfo().name, "handle open app, app id: {}", appID);
        openApp(appID);
    };
}

void AppLauncher::show_guide_page()
{
    if (!_is_first_open) {
        return;
    }

    if (!GetHAL().shouldShowGuide()) {
        return;
    }

    GetHAL().lvglLock();
    auto guide_page = std::make_unique<view::GuidePage>();
    GetHAL().lvglUnlock();

    _should_play_boot_sfx = false;
    GetHAL().playBootSfx();

    input::KeyManager key_manager;

    while (1) {
        GetHAL().feedTheDog();
        GetHAL().delay(50);

        key_manager.update();
        if (key_manager.getEvent() == input::KeyEvent::GoHome) {
            break;
        }
    }

    LvglLockGuard lock;
    guide_page.reset();
}

/*
 * main.cpp — 司南入口。
 *
 * 三条通道各起各的，任何一条起不来都不影响其他两条。
 * 这是架构约束，不要在这里加"等 WiFi 好了再起 BLE"之类的顺序依赖。
 *
 * 开机即壳：寐（团团）是默认脸。launcher 退役，Settings 由壳内手势直达。
 */
#include <apps/apps.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <sinan/bridge_ble.h>
#include <sinan/bridge_hid.h>
#include <sinan/bridge_ws.h>
#include <sinan/config.h>
#include <sinan/debug_cli.h>
#include <sinan/haptics.h>
#include <sinan/photo_store.h>
#include <sinan/precession.h>
#include <sinan/shell.h>
#include <sinan/state.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    GetHAL().init();

    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // 岁差根容器要在任何 UI 之前就位
    {
        LvglLockGuard lock;
        sinan::ui::precession_root();
    }

    // 多感官语义层：全机的震动与提示音都从这里出
    sinan::haptics::init();

    // 照片仓库开机扫描（解码发生在 Rest 层创建时，不在网络回调里）
    sinan::photo::init();

    // BLE：Buddy NUS + HID 键盘同一主机。不需要配网，桌面端扫到就能连
    sinan::ble::start();
#if SN_HID_ENABLE
    sinan::hid::start();
#endif

    // WiFi + WS：连不上也无所谓，审批与 HID 照常工作
#if SN_WS_ENABLE
    sinan::ws::start(SN_WIFI_SSID, SN_WIFI_PASS, SN_WS_URI, SN_WS_TOKEN);
#endif

    const int id_shell = GetMooncake().installApp(std::make_unique<AppSinan>());
    const int id_setup = GetMooncake().installApp(std::make_unique<AppSetup>());
    sinan::shell::set_settings_app_id(id_setup);
    GetMooncake().openApp(id_shell);

    sinan::debug::start();

    while (1) {
        GetHAL().feedTheDog();
        sinan::ui::precession_tick(GetHAL().millis());
        GetMooncake().update();
    }
}

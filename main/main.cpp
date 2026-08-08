/*
 * main.cpp — 司南入口。
 *
 * 三条通道各起各的，任何一条起不来都不影响其他两条。
 * 这是架构约束，不要在这里加"等 WiFi 好了再起 BLE"之类的顺序依赖。
 */
#include <apps/apps.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <sinan/bridge_ble.h>
#include <sinan/bridge_ws.h>
#include <sinan/config.h>
#include <sinan/danger.h>
#include <sinan/precession.h>
#include <sinan/route.h>
#include <sinan/runtime_config.h>
#include <sinan/state.h>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <cstring>
#include <cstdio>

using namespace mooncake;
using namespace smooth_ui_toolkit;

namespace {

void ensure_rtc_sane()
{
    const auto current = GetHAL().getDateYmd();
    if (current.isValid() && current.year >= 2024 && current.year <= 2035) return;

    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4]{};
    int day = 1, year = 2026, hour = 0, minute = 0, second = 0;
    if (std::sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3 ||
        std::sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second) != 3) return;
    const char* p = std::strstr(months, mon);
    const int month = p ? static_cast<int>((p - months) / 3) + 1 : 1;

    GetHAL().setTimezone("UTC-8");
    GetHAL().setDateYmd(DateYmd{static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)});
    GetHAL().setTimeHms(TimeHms{static_cast<uint8_t>(hour), static_cast<uint8_t>(minute), static_cast<uint8_t>(second)});
    mclog::tagWarn("sinan", "RTC was invalid; seeded from firmware build time");
}

}  // namespace

extern "C" void app_main(void)
{
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    GetHAL().init();
    ensure_rtc_sane();
    GetHAL().playBootSfx();   // 官方开机音，白送的仪式感

    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // 岁差根容器要在任何应用打开之前就位。它默认是隐藏的 ——
    // 全屏不透明纯黑，不隐藏会把 launcher 整个盖掉
    {
        LvglLockGuard lock;
        sinan::ui::precession_root();
    }

    // 危险规则表改坏了会静默失效 —— 漏报没有代价可言，所以开机自测一次。
    // 只 log 不阻塞启动：设备不能因为一条规则不对就开不了机
    if (const int bad = sinan::selftest(); bad > 0) {
        mclog::tagError("sinan", "danger selftest: {} fixture(s) failed", bad);
    }

    // BLE：不需要配网，桌面端扫到就能连
    sinan::ble::start();

    // WiFi + WS：优先使用设备本地配网页保存的配置。配对令牌只存 NVS，
    // 不写入源码、不出现在日志。没有配置时保留纯离线照片与 BLE。
    const auto bridge = sinan::runtime_config::load();
    if (bridge.configured()) {
        sinan::ws::start(bridge.ssid.c_str(), bridge.password.c_str(),
                         bridge.uri.c_str(), bridge.token.c_str());
    } else {
        mclog::tagWarn("sinan", "WiFi bridge not configured; open Tools > Connect");
    }

    auto launcher = std::make_unique<AppLauncher>();
    auto* launcher_ptr = launcher.get();
    GetMooncake().installApp(std::move(launcher));
    sinan::route::bind_launcher(launcher_ptr);
    // 旧 AppGaze 的 FAT/LVGL 图片页不再占用 Photos 名称，
    // 由直接嵌入 JPEG 的狗狗照片页接管。
    sinan::route::register_app("Photos",  GetMooncake().installApp(std::make_unique<AppDogPhoto>()));
    sinan::route::register_app("Agarwood", GetMooncake().installApp(std::make_unique<AppWenwan>()));
    sinan::route::register_app("Ward",     GetMooncake().installApp(std::make_unique<AppWard>()));
    sinan::route::register_app("Fleet",    GetMooncake().installApp(std::make_unique<AppFleet>()));
    sinan::route::register_app("Tools",    GetMooncake().installApp(std::make_unique<AppTools>()));
    sinan::route::register_app("Connect",  GetMooncake().installApp(std::make_unique<AppConnect>()));
    sinan::route::register_app("Settings", GetMooncake().installApp(std::make_unique<AppSetup>()));

    /*
     * 官方原厂应用。它们画在 lv_screen_active()，司南画在岁差根容器上，
     * 而根容器在引用计数归零时隐藏 —— 所以两套可以共存，互不遮挡。
     * 改动根容器可见性策略的人要保住这一点。
     */
    sinan::route::register_app("Stopwatch", GetMooncake().installApp(std::make_unique<AppStopWatch>()));
    sinan::route::register_app("Badge", GetMooncake().installApp(std::make_unique<AppBadge>()));
#if SINAN_DIAG
    // 上板自检用：麦克风是不是坏的、IMU 有没有数据。
    // 第一次点不亮时，先用它们排除硬件，再回来怪自己的代码
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppFft>());
#endif

    // 配网不是开机门槛。没有 Wi-Fi 时仍先展示照片桌面，Work 只显示离线；
    // 用户需要联网时再从 Tools 主动进入 Connect。配置页会占用 Wi-Fi AP，
    // 绝不能把未配网用户困在这里、让整台设备看起来只有一张表单。
    sinan::route::open(SN_BOOT_APP);

    while (1) {
        GetHAL().feedTheDog();
        sinan::ui::precession_tick(GetHAL().millis());   // 内部自己持 LVGL 锁
        GetMooncake().update();
        sinan::route::tick();
    }
}

#include "route.h"
#include "state.h"
#include <apps/app_launcher/app_launcher.h>
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cstring>
#include <string>
#include <vector>

namespace sinan::route {

namespace {

constexpr char TAG[] = "sinan.route";

struct Entry {
    std::string name;
    int id;
};

std::vector<Entry> s_apps;
AppLauncher* s_launcher = nullptr;
int s_pending = -1;
bool s_auto_opened = false;
std::string s_home;
uint32_t s_last_open = 0;   // 上次把守推到前台的时刻

int id_of(const char* name)
{
    if (!name || !*name) return -1;
    for (const auto& e : s_apps) {
        if (e.name == name) return e.id;
    }
    return -1;
}

}  // namespace

void bind_launcher(AppLauncher* launcher)
{
    s_launcher = launcher;
}

void register_app(const char* name, int id)
{
    s_apps.push_back({name, id});
}

bool open(const char* name)
{
    const int id = id_of(name);
    if (id < 0 || !s_launcher) return false;
    if (s_home.empty()) s_home = name;   // 第一个被显式打开的就是开机应用

    const int current = s_launcher->getRunningAppId();
    if (current == id && s_pending < 0) return true;
    if (current >= 0 &&
        mooncake::GetMooncake().getAppCurrentState(current) != mooncake::AppAbility::StateSleeping) {
        mooncake::GetMooncake().closeApp(current);
    }
    s_pending = id;
    return true;
}

void tick()
{
    if (!s_launcher || s_pending < 0) return;

    const int current = s_launcher->getRunningAppId();
    if (current >= 0 &&
        mooncake::GetMooncake().getAppCurrentState(current) != mooncake::AppAbility::StateSleeping) {
        return;
    }

    if (s_launcher->openApp(s_pending)) s_pending = -1;
}

bool yield_to_ward_if_needed()
{
    const auto s = State::get().snapshot();

    /*
     * 两件事都要把守推到前台：
     *   1. 有待决请求
     *   2. 正在配对（六位码只有守画得出来，看不见码就配不上对）
     * 用 key 去重，否则每帧都 openApp 一次，动画永远重来。
     */
    const bool want = (s.ble.passkey != 0) || s.ble.has_prompt;
    if (!want) return false;

    const int id = id_of("Ward");
    if (id < 0) return false;

    /*
     * 去重用时间窗，不要用 prompt id 做 key。
     *
     * 按 key 去重看起来更精确，但会造成死局：用户在请求待决时手动从守退回
     * launcher 再进望，望调到这里发现 key 没变就直接 return true —— 望每帧
     * 什么都不做，画面卡住，而守也没被拉起来。
     *
     * 而调用方永远只会是非守应用，它还在跑就说明守不在前台，那就该切。
     * 时间窗只是防止 openApp 尚未生效的那一两帧里重复调用。
     */
    const uint32_t now = GetHAL().millis();
    if (now - s_last_open < 1000) return true;

    s_last_open   = now;
    s_auto_opened = true;
    open("Ward");
    mclog::tagInfo(TAG, "switching to Ward");
    return true;
}

void return_home_if_auto()
{
    s_last_open = 0;
    if (!s_auto_opened) return;
    s_auto_opened = false;
    open(s_home.empty() ? "Photos" : s_home.c_str());
}

}  // namespace sinan::route

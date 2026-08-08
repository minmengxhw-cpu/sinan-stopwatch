/*
 * route.h — 前台应用路由。
 *
 * 职责边界：只回答"现在该让哪个应用在前台"，不碰 UI。
 *
 * 存在的理由：有权限请求进来时，用户不应该还要自己去 launcher 里点开守。
 * 望/阵/问/历 在 onRunning 里各自调一次 yield_to_ward_if_needed()，
 * 请求处理完再回到开机应用。
 */
#pragma once

class AppLauncher;

namespace sinan::route {

// main.cpp 安装官方 launcher 时绑定一次。所有前台切换都必须经它完成，
// 否则 launcher 不知道哪个应用在运行，旧应用也不会被正确关闭。
void bind_launcher(AppLauncher* launcher);

// main.cpp 在 installApp 之后登记，id 就是 installApp 的返回值
void register_app(const char* name, int id);

// 按名字打开。名字不存在或为空则什么都不做，返回 false
bool open(const char* name);

// 每轮 Mooncake update 后调用：等当前应用真正 Sleeping 后再交给 launcher
// 打开待切换目标，避免两个全屏应用在同一帧同时运行。
void tick();

/*
 * 有待决请求且当前不在守里 -> 打开守，返回 true（调用方应立即 return）。
 * 会记下"这次是自动切过去的"，好在请求处理完之后回得来。
 */
bool yield_to_ward_if_needed();

// 守在请求处理完后调。只有当初是自动切过来的才回开机应用
void return_home_if_auto();

}  // namespace sinan::route

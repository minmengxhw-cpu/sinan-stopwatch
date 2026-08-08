/*
 * app_shell.cpp — 司南壳。
 *
 * 一台设备一件器物：寐 / 阵 / Action / 中断全是它内部的层，
 * launcher 概念退役，Settings 由 Rest 层 B 长按直达。
 */
#include "app_shell.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <sinan/shell.h>

using namespace mooncake;

AppSinan::AppSinan() { setAppInfo().name = "Sinan"; }

void AppSinan::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
    open();  // 装上即前台，不等谁点名
}

void AppSinan::onOpen()
{
    if (!_shell_up) {
        _shell_up = true;
        sinan::shell::init();
        // 启动 Logo 的使命到壳起来为止。launcher 退役后没人替它收尸，
        // 留着只会白吃渲染
        GetHAL().bootLogo.reset();
    }
}

void AppSinan::onRunning()
{
    sinan::shell::tick(GetHAL().millis());
}

void AppSinan::onClose()
{
    // 壳不关。close 路径留着只为满足框架契约
}

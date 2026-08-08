/*
 * app_shell.h — 司南壳应用。mooncake 里唯一的常驻 App。
 */
#pragma once
#include <mooncake.h>

class AppSinan : public mooncake::AppAbility {
public:
    AppSinan();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    bool _shell_up = false;
};

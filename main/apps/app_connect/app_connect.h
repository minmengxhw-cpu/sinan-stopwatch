#pragma once

#include <apps/common/loading_page/loading_page.h>
#include <mooncake.h>
#include <memory>

class AppConnect : public mooncake::AppAbility {
public:
    AppConnect();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    bool _started = false;
    std::unique_ptr<view::LoadingPage> _page;
};

#pragma once

#include <mooncake.h>

class AppDogPlay : public mooncake::AppAbility {
public:
    AppDogPlay();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    int _index = 0;
    int _play_count = 0;
    bool _touch_was_down = false;
    int _touch_start_x = 0;
    int _touch_start_y = 0;
    int _touch_last_x = 0;
    int _touch_last_y = 0;

    void render(const char* action);
    void play(const char* action);
};

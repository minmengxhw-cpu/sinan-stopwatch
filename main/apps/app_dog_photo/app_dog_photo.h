#pragma once

#include <mooncake.h>

class AppDogPhoto : public mooncake::AppAbility {
public:
    AppDogPhoto();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    int _index = 0;
    bool _touch_was_down = false;
    int _touch_start_x = 0;
    int _touch_last_x = 0;

    void render();
    void move(int delta);
};

#pragma once

#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <memory>

class AppTools : public mooncake::AppAbility {
public:
    AppTools();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key;
    lv_obj_t* _stage = nullptr;
    lv_obj_t* _rim = nullptr;
    lv_obj_t* _title = nullptr;
    lv_obj_t* _sub = nullptr;
    lv_obj_t* _chord = nullptr;
    int _index = 0;

    void redraw();
};

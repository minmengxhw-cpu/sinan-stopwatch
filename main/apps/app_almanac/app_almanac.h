/*
 * app_almanac.h — 历：三个日常表盘（号码 / 黄历 / 年轮），A 键轮播。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <memory>

class AppAlmanac : public mooncake::AppAbility {
public:
    AppAlmanac();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage  = nullptr;
    lv_obj_t* _rim    = nullptr;
    lv_obj_t* _wheel  = nullptr;  // 年轮的 73 段刻度环
    lv_obj_t* _big    = nullptr;
    lv_obj_t* _sub    = nullptr;
    lv_obj_t* _chord  = nullptr;

    int _dial = 0;
    uint32_t _last_draw = 0;

    void draw_number();
    void draw_huangli();
    void draw_yearring();
    void redraw();
};

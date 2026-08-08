/*
 * app_echo.h — 问：按住说话，Mac 转写并执行，结果语音回来。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <array>
#include <memory>

class AppEcho : public mooncake::AppAbility {
public:
    AppEcho();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    // HAL 的频谱正好 20 段，沿圆周每段 18°，不多不少刚好一圈
    static constexpr int kBands = 20;

    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage = nullptr;
    lv_obj_t* _rim   = nullptr;
    std::array<lv_obj_t*, kBands> _bars{};
    lv_obj_t* _glyph = nullptr;
    lv_obj_t* _text  = nullptr;
    lv_obj_t* _chord = nullptr;

    bool _recording      = false;
    uint32_t _rec_start  = 0;
    uint32_t _wait_start = 0;   // 松手之后开始等结果的时刻
    int _face            = 0;   // 当前画的是哪一态，避免每帧重建
    float _spin          = 0.0f;

    void set_face(int f, const char* line, uint32_t hue);
    void draw_levels();
};

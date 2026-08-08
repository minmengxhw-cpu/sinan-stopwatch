/*
 * app_gaze.h — 望：团团待机页。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <array>
#include <memory>

class AppGaze : public mooncake::AppAbility {
public:
    AppGaze();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

    // 交叉淡入的 lv_timer 回调要调它，必须 public
    void onSwapDone();

private:
    static constexpr int kVignetteRings = 24;  // 晕影用同心弧堆，半径可运行时改做呼吸
    static constexpr int kTrailDots     = 6;   // 外圈点身后的拖痕

    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage      = nullptr;
    lv_obj_t* _photo      = nullptr;
    lv_obj_t* _photo_next = nullptr;
    lv_obj_t* _hint       = nullptr;
    std::array<lv_obj_t*, kVignetteRings> _vig{};
    lv_obj_t* _rim   = nullptr;   // 寐版式下的分钟进度弧
    lv_obj_t* _scrim = nullptr;
    lv_obj_t* _time  = nullptr;
    lv_obj_t* _date  = nullptr;
    lv_obj_t* _dot   = nullptr;
    std::array<lv_obj_t*, kTrailDots> _trail{};

    const lv_image_dsc_t* _cur_dsc  = nullptr;
    const lv_image_dsc_t* _next_dsc = nullptr;

    int _index    = 0;
    int _vig_inner = 174;   // 晕影内缘，随照片形态变
    int _layout   = 0;   // 0 寐(纯团团+分钟弧) / 1 时(底部时间) / 2 忆(好日子)
    lv_obj_t* _joy_ring = nullptr;   // 忆：好日子的刻度环
    lv_obj_t* _joy_cap  = nullptr;   // 忆：那天是什么日子
    lv_obj_t* _chord    = nullptr;   // 忆：照片日期
    bool _locked  = false;
    int _last_min = -1;
    bool _touch_was_down = false;
    int _touch_start_x = -1;
    int _touch_start_y = -1;
    int _touch_last_x  = -1;
    int _touch_last_y  = -1;

    uint32_t _t_photo = 0;
    uint32_t _t_dot   = 0;
    uint32_t _t_swap  = 0;
    uint32_t _t_woke  = 0;

    float _tilt_x = 0.0f;
    float _tilt_y = 0.0f;

    void build_vignette();
    void apply_layout();
    void tick_photo(uint32_t now);
    void tick_dot(uint32_t now);
    void tick_clock(bool force);
    void tick_rim();
    void build_joy();
    void show_joy(bool on);
    void swap_photo();
};

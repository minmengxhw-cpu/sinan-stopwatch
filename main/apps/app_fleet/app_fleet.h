/*
 * app_fleet.h — 阵：多模型 worker 的额度与状态雷达。
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <cstdint>
#include <array>
#include <memory>

namespace sinan {
struct FleetState;
}

class AppFleet : public mooncake::AppAbility {
public:
    AppFleet();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr int kMaxSeg = 3;
    std::unique_ptr<input::KeyManager> _key;

    lv_obj_t* _stage = nullptr;
    lv_obj_t* _rim   = nullptr;
    std::array<lv_obj_t*, kMaxSeg> _seg{};
    std::array<lv_obj_t*, kMaxSeg> _seg_bg{};
    std::array<lv_obj_t*, kMaxSeg> _seg_label{};
    std::array<lv_obj_t*, kMaxSeg> _seg_mark{};
    std::array<bool, kMaxSeg> _breathing{};   // 呼吸的边沿状态
    lv_obj_t* _focus_pct  = nullptr;
    lv_obj_t* _focus_name = nullptr;
    lv_obj_t* _body       = nullptr;
    lv_obj_t* _chord      = nullptr;

    int _focus = 0;
    bool _session = false;
    uint32_t _rec_start = 0;
    float _spin = 0.0f;
    uint32_t _last_draw = 0;

    void rebuild(const sinan::FleetState& f);
    void redraw();
    void redraw_selector(const sinan::FleetState& f, bool stale);
    void redraw_session();
    const char* target_id() const;
    const char* target_label() const;
};

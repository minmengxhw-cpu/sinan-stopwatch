#pragma once

#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <lvgl.h>
#include <memory>
#include <array>

// 纯赛博沉香：用一枚香、一段火候和一圈余韵把圆屏变成点香仪式。
class AppWenwan : public mooncake::AppAbility {
public:
    AppWenwan();
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<input::KeyManager> _key;
    lv_obj_t* _stage = nullptr;
    lv_obj_t* _rim = nullptr;
    lv_obj_t* _orbit = nullptr;
    lv_obj_t* _progress = nullptr;
    lv_obj_t* _stick = nullptr;
    lv_obj_t* _holder = nullptr;
    lv_obj_t* _flame = nullptr;
    std::array<lv_obj_t*, 3> _smoke{};
    lv_obj_t* _ember = nullptr;
    lv_obj_t* _title = nullptr;
    lv_obj_t* _phase_label = nullptr;
    lv_obj_t* _detail = nullptr;
    lv_obj_t* _chord = nullptr;
    enum class Phase : uint8_t { Select, Lighting, Burning, Afterglow };
    Phase _phase = Phase::Select;
    int _scent = 0;
    uint32_t _phase_started = 0;
    uint32_t _last_draw = 0;

    void redraw();
    void begin_ritual();
    void advance_phase();
    void reset_ritual();
    uint32_t phase_duration() const;
    float phase_ratio(uint32_t now) const;
};

/*
 * precession.cpp
 *
 * AMOLED 常亮必然烧屏。定时息屏会毁掉"常驻仪表"的核心价值，
 * 所以改成让整个画面以肉眼不可察的速度自转：0.3°/分钟，20 小时一周。
 * 任何像素都不会长期承担同一个亮点。
 */
#include "precession.h"
#include "design.h"
#include <hal/hal.h>

namespace sinan::ui {

using namespace sinan::design;

static lv_obj_t* s_root          = nullptr;
static uint32_t s_last_precess   = 0;
static uint32_t s_last_jitter    = 0;
static int32_t s_offset_decideg  = 0;
static int s_jitter_phase        = 0;
static Luma s_luma               = Luma::Normal;
static Luma s_luma_applied       = Luma::Normal;
static uint32_t s_luma_changed   = 0;
static int s_refs                = 0;

lv_obj_t* precession_root()
{
    if (s_root) return s_root;

    s_root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_root, SCREEN, SCREEN);
    lv_obj_center(s_root);
    lv_obj_set_style_bg_color(s_root, c(INK), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    // 绕屏幕中心转，不是绕自身左上角
    lv_obj_set_style_transform_pivot_x(s_root, CENTER, 0);
    lv_obj_set_style_transform_pivot_y(s_root, CENTER, 0);
    lv_obj_set_style_transform_rotation(s_root, s_offset_decideg, 0);
    // 默认藏起来，否则这块全屏黑板会把 launcher 整个盖掉
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    return s_root;
}

void root_acquire()
{
    lv_obj_t* r = precession_root();
    if (s_refs++ == 0) {
        lv_obj_remove_flag(r, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(r);
    }
}

void root_release()
{
    if (s_refs > 0 && --s_refs == 0 && s_root) {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void precession_tick(uint32_t now_ms)
{
    if (!s_root) return;

    // 这里会动 LVGL 对象，而调用方是主循环，没有别处帮它上锁
    LvglLockGuard lock;

    if (now_ms - s_last_precess >= PRECESS_INTERVAL_MS) {
        s_last_precess = now_ms;
        s_offset_decideg = (s_offset_decideg + PRECESS_STEP_DECIDEG) % 3600;
        lv_obj_set_style_transform_rotation(s_root, s_offset_decideg, 0);
    }

    // 大字数字是最容易烧的区域，额外给一层慢抖动
    if (now_ms - s_last_jitter >= JITTER_INTERVAL_MS) {
        s_last_jitter = now_ms;
        s_jitter_phase = (s_jitter_phase + 1) % 4;
        static const int dx[4] = {JITTER_PX, 0, -JITTER_PX, 0};
        static const int dy[4] = {0, JITTER_PX, 0, -JITTER_PX};
        lv_obj_set_style_translate_x(s_root, dx[s_jitter_phase], 0);
        lv_obj_set_style_translate_y(s_root, dy[s_jitter_phase], 0);
    }

    // 亮度去抖：连续 600ms 保持同一意图才真正下发，避免闪烁
    if (s_luma != s_luma_applied) {
        if (s_luma_changed == 0) {
            s_luma_changed = now_ms;
        } else if (now_ms - s_luma_changed > 600) {
            int target = BL_NORMAL;
            if (s_luma == Luma::Quiet) target = BL_QUIET;
            else if (s_luma == Luma::Alert) target = BL_ALERT;
            GetHAL().setBackLightBrightness(target);
            s_luma_applied = s_luma;
            s_luma_changed = 0;
        }
    } else {
        s_luma_changed = 0;
    }
}

void set_luma(Luma l) { s_luma = l; }

void precession_reset()
{
    s_offset_decideg = 0;
    if (s_root) {
        lv_obj_set_style_transform_rotation(s_root, 0, 0);
        lv_obj_set_style_translate_x(s_root, 0, 0);
        lv_obj_set_style_translate_y(s_root, 0, 0);
    }
}

int32_t precession_offset_decideg() { return s_offset_decideg; }

}  // namespace sinan::ui

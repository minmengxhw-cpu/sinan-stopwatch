/*
 * ring.cpp — 圆形 UI 原语实现。原生 LVGL 9.5 C API，不依赖包装类。
 */
#include "ring.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sinan::ui {

using namespace sinan::design;

static constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;

void place_polar(lv_obj_t* o, int r, float deg)
{
    // 12 点为 0°，顺时针。屏幕 y 轴向下，所以 y 用负 cos
    const float rad = (deg - 90.0f) * kDeg2Rad;
    const int x = static_cast<int>(std::lround(r * std::cos(rad)));
    const int y = static_cast<int>(std::lround(r * std::sin(rad)));
    lv_obj_align(o, LV_ALIGN_CENTER, x, y);
}

lv_obj_t* arc(lv_obj_t* parent, const ArcSpec& spec, float start_deg, float end_deg)
{
    lv_obj_t* a = lv_arc_create(parent);
    const int d = spec.radius * 2 + spec.width;
    lv_obj_set_size(a, d, d);
    lv_obj_center(a);

    // 弧是纯展示件：去掉旋钮、禁掉点击，否则触摸会误改数值
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(a, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(a, 0, LV_PART_MAIN);

    lv_obj_set_style_arc_width(a, spec.width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, spec.width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, spec.rounded, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, spec.rounded, LV_PART_INDICATOR);

    if (spec.track == INK) {
        lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
        lv_obj_set_style_arc_color(a, c(spec.track), LV_PART_MAIN);
    }
    lv_obj_set_style_arc_color(a, c(spec.color), LV_PART_INDICATOR);

    lv_arc_set_bg_angles(a, 0, 360);
    arc_set_range(a, start_deg, end_deg);
    return a;
}

void arc_set_range(lv_obj_t* a, float start_deg, float end_deg)
{
    int s = to_lv_angle(start_deg);
    int e = to_lv_angle(end_deg);
    while (s < 0) s += 360;
    while (e < 0) e += 360;
    lv_arc_set_angles(a, s, e);
}

void arc_set_color(lv_obj_t* a, uint32_t hex)
{
    lv_obj_set_style_arc_color(a, c(hex), LV_PART_INDICATOR);
}

void arc_set_progress(lv_obj_t* a, float ratio)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    // 359.9 而不是 360：LVGL 里 start==end 会被判成空弧，满圈会突然消失
    arc_set_range(a, 0.0f, ratio >= 1.0f ? 359.9f : ratio * 360.0f);
}

struct ArcAnimCtx {
    float start;
    float from;
    float to;
};

static void arc_anim_exec(void* obj, int32_t v)
{
    lv_obj_t* a = static_cast<lv_obj_t*>(obj);
    auto* ctx = static_cast<ArcAnimCtx*>(lv_obj_get_user_data(a));
    if (!ctx) return;
    const float e = ctx->from + (ctx->to - ctx->from) * (v / 1000.0f);
    arc_set_range(a, ctx->start, e);
}

void arc_animate_to(lv_obj_t* a, float start_deg, float end_deg, uint32_t ms)
{
    auto* ctx = static_cast<ArcAnimCtx*>(lv_obj_get_user_data(a));
    if (!ctx) {
        ctx = new ArcAnimCtx{};
        lv_obj_set_user_data(a, ctx);
    }
    // 从当前终点接着动，避免连续调用时的跳变
    int32_t cur_s = 0, cur_e = 0;
    cur_s = lv_arc_get_angle_start(a);
    cur_e = lv_arc_get_angle_end(a);
    (void)cur_s;
    ctx->start = start_deg;
    ctx->from  = static_cast<float>(cur_e) + 90.0f;
    ctx->to    = end_deg;

    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, a);
    lv_anim_set_exec_cb(&an, arc_anim_exec);
    lv_anim_set_values(&an, 0, 1000);
    lv_anim_set_time(&an, ms);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_start(&an);
}

lv_obj_t* ticks(lv_obj_t* parent, int radius, int count, int len, int thickness, uint32_t color)
{
    lv_obj_t* ring = lv_obj_create(parent);
    lv_obj_set_size(ring, SCREEN, SCREEN);
    lv_obj_center(ring);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, 0, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < count; i++) {
        const float deg = 360.0f * i / count;
        lv_obj_t* t = lv_obj_create(ring);
        lv_obj_set_size(t, thickness, len);
        lv_obj_set_style_radius(t, thickness / 2, 0);
        lv_obj_set_style_bg_color(t, c(color), 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        // 刻度自身要沿半径方向立起来
        lv_obj_set_style_transform_rotation(t, static_cast<int32_t>(deg * 10), 0);
        lv_obj_set_style_transform_pivot_x(t, thickness / 2, 0);
        lv_obj_set_style_transform_pivot_y(t, len / 2, 0);
        place_polar(t, radius, deg);
    }
    return ring;
}

void tick_set_color(lv_obj_t* ring, int index, uint32_t hex)
{
    if (index < 0 || index >= static_cast<int>(lv_obj_get_child_count(ring))) return;
    lv_obj_set_style_bg_color(lv_obj_get_child(ring, index), c(hex), 0);
}

void ticks_reset_color(lv_obj_t* ring, uint32_t hex)
{
    const uint32_t n = lv_obj_get_child_count(ring);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_set_style_bg_color(lv_obj_get_child(ring, i), c(hex), 0);
    }
}

lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, s ? s : "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

lv_obj_t* numeral(lv_obj_t* parent, const char* s)
{
    lv_obj_t* l = text(parent, s, SN_FONT_NUM_XL, SILK);
    lv_obj_center(l);
    numeral_set(l, s);
    return l;
}

void numeral_set(lv_obj_t* label, const char* s)
{
    if (!s) return;
    lv_label_set_text(label, s);
    // 位数多了自动降一档，避免撑破 Core 带
    lv_obj_set_style_text_font(label, std::strlen(s) > 4 ? SN_FONT_NUM_L : SN_FONT_NUM_XL, 0);
}

lv_obj_t* chord(lv_obj_t* parent, const char* s, uint32_t color)
{
    lv_obj_t* l = text(parent, s, SN_FONT_MONO_S, color);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, Y_CHORD);
    return l;
}

lv_obj_t* mono_block(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color, int width)
{
    lv_obj_t* l = text(parent, s, font, color);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(l, 6, 0);
    lv_obj_center(l);
    return l;
}

void breathe(lv_obj_t* o, uint32_t period_ms, int opa_lo, int opa_hi)
{
    if (period_ms < T_BREATH) period_ms = T_BREATH;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_style_arc_opa(static_cast<lv_obj_t*>(obj), v, LV_PART_INDICATOR);
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), v, 0);
    });
    lv_anim_set_values(&a, opa_lo, opa_hi);
    lv_anim_set_time(&a, period_ms / 2);
    lv_anim_set_playback_time(&a, period_ms / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

void stop_breathe(lv_obj_t* o)
{
    lv_anim_delete(o, nullptr);
    lv_obj_set_style_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_arc_opa(o, LV_OPA_COVER, LV_PART_INDICATOR);
}

void bloom(lv_obj_t* a, float from_deg, uint32_t hex, uint32_t ms)
{
    arc_set_color(a, hex);
    arc_set_range(a, from_deg - 2.0f, from_deg + 2.0f);
    // 两侧同时张开：起点往回退，终点往前推
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, a);
    lv_anim_set_user_data(&an, reinterpret_cast<void*>(static_cast<intptr_t>(from_deg)));
    lv_anim_set_exec_cb(&an, [](void* obj, int32_t v) {
        lv_obj_t* arc_obj = static_cast<lv_obj_t*>(obj);
        const float half = v / 10.0f;
        arc_set_range(arc_obj, -half, half);
    });
    lv_anim_set_values(&an, 20, 1800);
    lv_anim_set_time(&an, ms);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    lv_anim_start(&an);
}

lv_obj_t* stage(lv_obj_t* parent)
{
    lv_obj_t* s = lv_obj_create(parent);
    lv_obj_set_size(s, SCREEN, SCREEN);
    lv_obj_center(s);
    lv_obj_set_style_bg_color(s, c(INK), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    lv_obj_set_style_radius(s, SCREEN / 2, 0);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

}  // namespace sinan::ui

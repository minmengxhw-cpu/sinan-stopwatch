/*
 * ring.h — 圆形 UI 原语。
 *
 * 职责边界：把"在圆盘上画东西"这件事收敛成十来个函数，
 * 应用层不应该直接调 lv_arc_create / 手算极坐标。
 * 角度约定：0° = 12 点方向，顺时针为正。（LVGL 原生 0° 在 3 点，此处已换算）
 */
#pragma once
#include "design.h"
#include <lvgl.h>
#include <cstdint>
#include <vector>

namespace sinan::ui {

/* --------------------------- 极坐标 --------------------------- */

// 把元素放到以圆心为原点、半径 r、方位角 deg 的位置（元素自身居中对齐）
void place_polar(lv_obj_t* o, int r, float deg);

// 12 点为 0° 的角度转 LVGL 弧度角（3 点为 0°）
inline int to_lv_angle(float deg) { return static_cast<int>(deg) - 90; }

/* --------------------------- 弧 --------------------------- */

struct ArcSpec {
    int radius = design::R_RIM;
    int width  = design::W_RIM;
    uint32_t color = design::BRONZE;
    uint32_t track = design::LACQUER;  // 底槽色。传 design::INK 表示无底槽
    bool rounded   = true;
};

// 建一条弧。start/end 用 12 点为 0° 的角度制
lv_obj_t* arc(lv_obj_t* parent, const ArcSpec& spec, float start_deg, float end_deg);

void arc_set_range(lv_obj_t* a, float start_deg, float end_deg);
void arc_set_color(lv_obj_t* a, uint32_t hex);

// 从 12 点起、按比例 0.0–1.0 生长的进度弧
void arc_set_progress(lv_obj_t* a, float ratio);

// 带缓动地改变弧的终点角。用于"生长/退让"，替代所有滑入滑出
void arc_animate_to(lv_obj_t* a, float start_deg, float end_deg, uint32_t ms);

/* --------------------------- 刻度环 --------------------------- */

// 沿圆周均匀撒 count 个刻度。返回容器，刻度是它的子对象，按索引顺序排列
lv_obj_t* ticks(lv_obj_t* parent, int radius, int count, int len, int thickness, uint32_t color);

// 单独染色某一个刻度（索引从 12 点开始顺时针）
void tick_set_color(lv_obj_t* ring, int index, uint32_t hex);
void ticks_reset_color(lv_obj_t* ring, uint32_t hex);

/* --------------------------- 文字 --------------------------- */

lv_obj_t* text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color);

// Core 带的主数字。自动居中、自动按位数选字号
lv_obj_t* numeral(lv_obj_t* parent, const char* s);
void numeral_set(lv_obj_t* label, const char* s);

// 弦区标签：屏幕下方居中的一行小字
lv_obj_t* chord(lv_obj_t* parent, const char* s, uint32_t color = design::BRONZE);

// 等宽正文，自动折行并限宽在 Core 带内。用于命令、路径这类不能变形的内容
lv_obj_t* mono_block(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color, int width);

/* --------------------------- 动效 --------------------------- */

// 透明度呼吸。period 会被夹到 design::T_BREATH 以上
void breathe(lv_obj_t* o, uint32_t period_ms, int opa_lo = 60, int opa_hi = 255);
void stop_breathe(lv_obj_t* o);

// 弧从某个角度向两侧展开成整圈，用于决策落定的反馈
void bloom(lv_obj_t* a, float from_deg, uint32_t hex, uint32_t ms);

/* --------------------------- 屏幕基座 --------------------------- */

// 纯黑全屏底板。每个应用 onOpen 时挂在 precession_root() 上
lv_obj_t* stage(lv_obj_t* parent);

}  // namespace sinan::ui

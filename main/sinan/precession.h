/*
 * precession.h — 岁差：防烧屏，同时是本机的签名视觉。
 *
 * 职责边界：提供所有应用共用的根容器，并让它极缓慢自转。
 * 应用不要直接挂 lv_screen_active()，否则该应用的画面会长期常亮同一批像素。
 */
#pragma once
#include <lvgl.h>
#include <cstdint>

namespace sinan::ui {

// 拿根容器。首次调用时创建，之后返回同一个
lv_obj_t* precession_root();

// 主循环里每帧调一次。内部自己控节奏，调多了不会加速
void precession_tick(uint32_t now_ms);

// 亮度意图。由应用声明当前该多亮，实际下发做了平滑与去抖
enum class Luma { Quiet, Normal, Alert };
void set_luma(Luma l);

// 重置到零位。仅用于自测确认岁差确实在动
void precession_reset();

// 已累计转过的角度（0.1° 为单位），自测用
int32_t precession_offset_decideg();

}  // namespace sinan::ui

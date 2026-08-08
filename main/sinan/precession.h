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

/*
 * 根容器是全屏不透明纯黑，会盖住上游的 launcher。
 * 所以它默认隐藏，由应用在 onOpen/onClose 成对 acquire/release。
 * 忘了 release 的后果是开机一片全黑，看起来像板子坏了。
 *
 * 【锁契约】这两个函数**要求调用方已经持有 LVGL 锁**，内部不再上锁。
 * 早期版本内部自己 LvglLockGuard，而四个应用是"先拿锁再调 acquire" ——
 * 非递归锁下这是死锁。契约统一成这样之后，两边写法都只有一种：
 *
 *   onOpen :  LvglLockGuard lock;  root_acquire();  建 UI
 *   onClose:  LvglLockGuard lock;  删 stage;  root_release();
 */
void root_acquire();
void root_release();

// 主循环里每帧调一次。内部自己持 LVGL 锁，调用方不用再包一层
void precession_tick(uint32_t now_ms);

// 亮度意图。由应用声明当前该多亮，实际下发做了平滑与去抖
enum class Luma { Quiet, Normal, Alert };
void set_luma(Luma l);

// 重置到零位。仅用于自测确认岁差确实在动
void precession_reset();

// 已累计转过的角度（0.1° 为单位），自测用
int32_t precession_offset_decideg();

}  // namespace sinan::ui

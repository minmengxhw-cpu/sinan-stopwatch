/*
 * design.h — 司南视觉令牌。
 *
 * 职责边界：整个固件唯一允许定义颜色、半径、字号、动画时长的地方。
 * 应用代码里出现字面量颜色或半径 = 设计系统失效。
 */
#pragma once
#include <assets/assets.h>
#include <lvgl.h>
#include <cstdint>

#if defined(SINAN_HAS_CJK_FONT)
// 生成文件以 C 编译，符号位于全局命名空间。
LV_FONT_DECLARE(lv_font_sinan_serif_28);
LV_FONT_DECLARE(lv_font_sinan_serif_40);
#endif

namespace sinan::design {

/* ------------------------------ 矿物色板 ------------------------------ */
/* 不用通用暗色主题那一套。底是黑漆，字是生宣，刻度是鎏金，
   语义色取自朱砂、石绿、靛青三种传统矿物颜料。*/

constexpr uint32_t INK       = 0x000000;  // 真黑。AMOLED 不发光，省电且不烧屏
constexpr uint32_t LACQUER   = 0x141210;  // 漆地。唯一允许的"接近黑"，只用于弧底槽
constexpr uint32_t BRONZE    = 0xC8A96E;  // 鎏金。刻度、次级标签
constexpr uint32_t BRONZE_D  = 0x6E5C3A;  // 鎏金暗部
constexpr uint32_t SILK      = 0xF2EDE1;  // 生宣。主数字、主文本
constexpr uint32_t SILK_D    = 0x8A857B;  // 生宣暗部
/* 语义色是一条**严重度轴**，不是场景标签。按严重度选，不要按"这是什么功能"选 */
constexpr uint32_t MALACHITE = 0x4FA88A;  // 石绿。好 / 通过 / 健康
constexpr uint32_t AMBER     = 0xE8A33D;  // 琥珀。需要你注意：等待决策、额度将尽、状态偏低
constexpr uint32_t CINNABAR  = 0xD6442F;  // 朱砂。**只用于不可逆操作，全设备只有守能用**
constexpr uint32_t INDIGO    = 0x3B4C8C;  // 靛青。没有信息：静默、断链、陈旧、无害的失败

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

/* ------------------------------ 几何 ------------------------------ */

constexpr int SCREEN = 466;
constexpr int CENTER = 233;
constexpr int R_MAX  = 233;
constexpr int R_SAFE = 213;  // 物理安全边。任何元素不得越过

/* 晕影按照片形态走。蜷成球的主体本身就是圆的，晕影可以收在外圈；
   坐姿站姿的照片背景多，晕影要往里压才吃得掉 */
constexpr int VIG_INNER_DISC     = 174;
constexpr int VIG_INNER_PORTRAIT = 140;
constexpr int VIG_OUTER          = 236;   // 必须越过 R_MAX，否则四角露白
constexpr int VIG_RINGS          = 24;

constexpr int R_RIM       = 219;  // Rim 环中线半径
constexpr int W_RIM       = 10;
constexpr int R_ORBIT     = 178;  // Orbit 环中线半径
constexpr int W_ORBIT     = 6;
constexpr int R_ORBIT_IN  = 150;
constexpr int R_CORE      = 145;
constexpr int Y_CHORD     = 372;  // 弦区基线（绝对 y）

/* ------------------------------ 运动 ------------------------------ */

constexpr uint32_t T_FAST   = 320;
constexpr uint32_t T_STATE  = 480;
constexpr uint32_t T_BREATH = 2600;  // 呼吸周期下限。快过这个显得焦躁

/* ------------------------------ 岁差防烧屏 ------------------------------ */

constexpr uint32_t PRECESS_INTERVAL_MS = 60UL * 1000;
constexpr int PRECESS_STEP_DECIDEG     = 3;  // 0.3°/min，20 小时一周
constexpr uint32_t JITTER_INTERVAL_MS  = 5UL * 60 * 1000;
constexpr int JITTER_PX                = 3;

/* ------------------------------ 亮度 ------------------------------ */

constexpr int BL_QUIET  = 25;
constexpr int BL_NORMAL = 60;
constexpr int BL_ALERT  = 90;

/* ------------------------------ 交互 ------------------------------ */

constexpr uint32_t LONGPRESS_MS = 800;
constexpr float TAP_G           = 1.85f;
constexpr uint32_t STALE_MS     = 30000;

/* ------------------------------ 字体 ------------------------------ */
/* 数字用 Commissioner（几何无衬线，字宽稳定）
   数据用 Maple Mono（等宽，命令与路径不会跳动）
   中文用思源宋体子集（横细竖粗，鎏金上有金石感；黑体会像手机 App）*/

/* 字体由上游 assets/assets.h 用 LV_FONT_DECLARE 声明在全局命名空间。
   早期版本在这里 extern "C" 重声明了一遍 —— 同一批 C 链接符号在两个
   命名空间各声明一次，部分编译器会报冲突。直接 include 上游的头。*/

#define SN_FONT_NUM_XL (&CommissionerMedium108)
#define SN_FONT_NUM_L  (&CommissionerMedium64)
#define SN_FONT_MONO_L (&lv_font_maple_mono_medium_48)
#define SN_FONT_MONO_M (&lv_font_maple_mono_medium_28)
#define SN_FONT_MONO_S (&lv_font_maple_mono_medium_24)

/* 中文子集字体由 scripts/build_cjk_font.sh 生成。
   未生成时回落到 Montserrat，中文显示为空白 —— 刻意如此，
   空白比乱码更容易在自测时被发现。*/
#if defined(SINAN_HAS_CJK_FONT)
#define SN_FONT_CJK_M (&::lv_font_sinan_serif_28)
#define SN_FONT_CJK_L (&::lv_font_sinan_serif_40)
#else
#define SN_FONT_CJK_M (&::lv_font_montserrat_28)
#define SN_FONT_CJK_L (&::lv_font_montserrat_36)
#endif

}  // namespace sinan::design

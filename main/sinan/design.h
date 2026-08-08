/*
 * design.h — 司南视觉令牌。
 *
 * 职责边界：整个固件唯一允许定义颜色、半径、字号、动画时长的地方。
 * 应用代码里出现字面量颜色或半径 = 设计系统失效。
 */
#pragma once
#include <lvgl.h>
#include <cstdint>

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
constexpr uint32_t CINNABAR  = 0xD6442F;  // 朱砂。危险、拒绝
constexpr uint32_t MALACHITE = 0x4FA88A;  // 石绿。健康、通过
constexpr uint32_t INDIGO    = 0x3B4C8C;  // 靛青。静默、待机、数据陈旧
constexpr uint32_t AMBER     = 0xE8A33D;  // 琥珀。等待决策，唯一的"催促"色

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

/* ------------------------------ 几何 ------------------------------ */

constexpr int SCREEN = 466;
constexpr int CENTER = 233;
constexpr int R_MAX  = 233;
constexpr int R_SAFE = 213;  // 物理安全边。任何元素不得越过

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

extern "C" {
extern const lv_font_t CommissionerMedium108;
extern const lv_font_t CommissionerMedium64;
extern const lv_font_t lv_font_maple_mono_medium_48;
extern const lv_font_t lv_font_maple_mono_medium_28;
extern const lv_font_t lv_font_maple_mono_medium_24;
}

#define SN_FONT_NUM_XL (&CommissionerMedium108)
#define SN_FONT_NUM_L  (&CommissionerMedium64)
#define SN_FONT_MONO_L (&lv_font_maple_mono_medium_48)
#define SN_FONT_MONO_M (&lv_font_maple_mono_medium_28)
#define SN_FONT_MONO_S (&lv_font_maple_mono_medium_24)

/* 中文子集字体由 scripts/build_cjk_font.sh 生成。
   未生成时回落到 Montserrat，中文显示为空白 —— 刻意如此，
   空白比乱码更容易在自测时被发现。*/
#if defined(SINAN_HAS_CJK_FONT)
extern "C" {
extern const lv_font_t lv_font_sinan_serif_28;
extern const lv_font_t lv_font_sinan_serif_40;
}
#define SN_FONT_CJK_M (&lv_font_sinan_serif_28)
#define SN_FONT_CJK_L (&lv_font_sinan_serif_40)
#else
#define SN_FONT_CJK_M (&lv_font_montserrat_28)
#define SN_FONT_CJK_L (&lv_font_montserrat_36)
#endif

}  // namespace sinan::design

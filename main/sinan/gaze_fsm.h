/*
 * gaze_fsm.h — 团团状态机。
 *
 * 职责边界：决定团团「现在是什么状态」，并保证每一次动效都能回答
 * 「因为什么」。状态转移全部走 gaze::notify，日志里可复盘每一次切换。
 *
 * 它不做绘制。Rest 层每帧读 mood() 与参数访问器，把状态翻译成
 * 点阵的不透明度/颜色、晕影的呼吸幅度。
 */
#pragma once
#include <cstdint>

namespace sinan::gaze {

enum class Mood : uint8_t {
    Sleep,   // 默认：呼吸慢，点阵 26%
    Hear,    // 链路上有新事件但未全屏：点阵亮起，Rim 琥珀一闪
    Alert,   // 全屏中断中：照片退暗，点阵极淡
    Praise,  // 刚批准：点阵染石绿，1 秒后回落
    Guard,   // 危险命令可见期：点阵染朱砂
    Pet,     // 被抚触：点阵短时生宣色
    Peek,    // 敲击/点中：露时间 6 秒
    Dream,   // 20 分钟无互动：珠串极慢自转
};

void init(uint32_t now);

// 状态迁移的唯一入口。reason 会进日志：gaze mood X->Y (reason)
void notify(Mood m, const char* reason, uint32_t now);

// 任何用户互动（触摸/按键/敲击）都调它，Dream 计时靠它
void poke(uint32_t now);

// 每帧。处理 Praise/Pet 的自然回落与 Sleep→Dream
void tick(uint32_t now);

Mood mood();
uint32_t since(uint32_t now);   // 在当前状态待了多久

/* ---- 给绘制层的参数 ---- */
int breath_px();        // 晕影呼吸幅度：Sleep 6 / Peek 3 / Alert 0
uint8_t ghost_opa();    // 点阵不透明度：Sleep 66 / Hear 120 / Alert 24
uint32_t ghost_hue();   // 点阵染色（design 语义色）
bool beads_dream();     // Dream 中：珠串允许极慢自转

}  // namespace sinan::gaze

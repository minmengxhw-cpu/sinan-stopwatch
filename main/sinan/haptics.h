/*
 * haptics.h — 多感官语义层。
 *
 * 职责边界：全机唯一允许发起震动与提示音的地方。
 * 应用不再裸调 GetHAL().vibrate() —— 它们说「发生了什么」（Cue），
 * 这里决定「听起来/摸起来是什么」。目标是闭眼能分清 OK 和 NG。
 */
#pragma once
#include <cstdint>

namespace sinan::haptics {

enum class Cue : uint8_t {
    Ok,     // 批准 / 正向确认：上行两音 + 中振一下
    Ng,     // 拒绝：下行两音 + 偏强一振
    Warn,   // 危险出现 / Grave 进入：低长音 + 双振
    Tick,   // 轻反馈：切 agent、拨珠一格。极短
    Pet,    // 呼噜：递减三连轻振，无声
    Alert,  // 新 prompt / ERROR：中音 + 长短双振
    Pair,   // 配对成功：上行三音 + 双短振
    Soft,   // 通用轻触：一轻振，无声
};

struct Settings {
    uint8_t vib_pct  = 80;   // 震动强度 0–100
    uint8_t aud_pct  = 60;   // 音量 0–100，0 = 静音（仍有振）
    bool state_vib   = true; // agent 状态变化是否振
};

void init();                 // HAL init 之后调一次。建播放任务、合成音表
void set(const Settings& s);
Settings get();

// 非阻塞：只投递到播放队列，可在 LVGL 锁内调用。新 Cue 打断旧的
void play(Cue c);

// 仅震动（对讲录音中等需要静音的场景）
void vibe_only(Cue c);

}  // namespace sinan::haptics

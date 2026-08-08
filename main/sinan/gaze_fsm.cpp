/*
 * gaze_fsm.cpp
 *
 * 转移表（唯一允许的「动」）：
 *   * → Hear    链路上有新事件且未进 Interrupt
 *   * → Alert   shell 进入 Interrupt
 *   * → Praise  批准成功（1.0s 后自动回 Sleep）
 *   * → Guard   Grave 展示中
 *   * → Pet     Rim 慢滑抚触（1.2s 后回 Sleep）
 *   * → Peek    敲击或中心轻点（6s 由 Rest 层计时收回）
 *   Sleep→Dream 20 分钟无互动（珠串极慢自转，每 60s 一格）
 */
#include "gaze_fsm.h"
#include "design.h"
#include <esp_log.h>

namespace sinan::gaze {
namespace {

constexpr char TAG[] = "sinan.gaze";

constexpr uint32_t kPraiseHold = 1000;
constexpr uint32_t kPetHold    = 1200;
constexpr uint32_t kDreamAfter = 20UL * 60 * 1000;

Mood s_mood = Mood::Sleep;
uint32_t s_entered = 0;
uint32_t s_last_poke = 0;

const char* name_of(Mood m)
{
    switch (m) {
        case Mood::Sleep:  return "Sleep";
        case Mood::Hear:   return "Hear";
        case Mood::Alert:  return "Alert";
        case Mood::Praise: return "Praise";
        case Mood::Guard:  return "Guard";
        case Mood::Pet:    return "Pet";
        case Mood::Peek:   return "Peek";
        default:           return "Dream";
    }
}

void transit(Mood m, const char* reason, uint32_t now)
{
    if (m == s_mood) return;
    ESP_LOGI(TAG, "gaze mood %s->%s (%s)", name_of(s_mood), name_of(m), reason ? reason : "-");
    s_mood = m;
    s_entered = now;
}

}  // namespace

void init(uint32_t now)
{
    s_mood = Mood::Sleep;
    s_entered = now;
    s_last_poke = now;
}

void notify(Mood m, const char* reason, uint32_t now)
{
    if (s_entered == 0) s_entered = now;
    transit(m, reason, now);
}

void poke(uint32_t now)
{
    s_last_poke = now;
    if (s_mood == Mood::Dream) transit(Mood::Sleep, "poke", now);
}

void tick(uint32_t now)
{
    if (s_entered == 0) s_entered = now;
    if (s_last_poke == 0) s_last_poke = now;

    switch (s_mood) {
        case Mood::Praise:
            if (now - s_entered > kPraiseHold) transit(Mood::Sleep, "praise done", now);
            break;
        case Mood::Pet:
            if (now - s_entered > kPetHold) transit(Mood::Sleep, "pet done", now);
            break;
        case Mood::Sleep:
            if (now - s_last_poke > kDreamAfter) transit(Mood::Dream, "quiet 20min", now);
            break;
        default:
            break;
    }
}

Mood mood() { return s_mood; }

uint32_t since(uint32_t now) { return now - s_entered; }

int breath_px()
{
    switch (s_mood) {
        case Mood::Sleep:
        case Mood::Dream:  return 6;
        case Mood::Peek:
        case Mood::Hear:   return 3;
        default:           return 0;   // Alert/Guard/Praise/Pet：有事的时候不喘
    }
}

uint8_t ghost_opa()
{
    switch (s_mood) {
        case Mood::Hear:   return 120;
        case Mood::Alert:
        case Mood::Guard:  return 24;
        case Mood::Praise:
        case Mood::Pet:    return 200;
        default:           return 66;  // 26%
    }
}

uint32_t ghost_hue()
{
    switch (s_mood) {
        case Mood::Praise: return design::MALACHITE;
        case Mood::Guard:  return design::CINNABAR;
        case Mood::Pet:    return design::SILK;
        case Mood::Hear:   return design::AMBER;
        default:           return design::BRONZE_D;
    }
}

bool beads_dream() { return s_mood == Mood::Dream; }

}  // namespace sinan::gaze

/*
 * haptics.cpp
 *
 * 一个低优先级播放任务消化 Cue 队列：震动节拍与方波音型在这里排程，
 * 调用方投递完就返回，UI 线程永远不被播放阻塞。
 *
 * 音表在 init/set 时一次性合成（方波 + 5ms 羽化边，防爆音），
 * 播放时只做 audioPlay 的指针传递，运行期不做任何波形计算。
 */
#include "haptics.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <cmath>
#include <vector>

namespace sinan::haptics {
namespace {

/* --------------------------- 音型与振动型 --------------------------- */

struct Note { uint16_t freq; uint16_t ms; };       // freq=0 表示休止
struct Pulse { uint16_t ms; uint16_t gap; };       // gap=结束后静默

struct Pattern {
    Pulse pulses[3];
    Note notes[3];
};

// 语义表。改听感只改这里，不许散到应用里
const Pattern& pattern_of(Cue c)
{
    static const Pattern kOk      = {{{90, 0},   {0, 0},    {0, 0}},
                                     {{660, 70}, {880, 130}, {0, 0}}};
    static const Pattern kNg      = {{{160, 0},  {0, 0},    {0, 0}},
                                     {{440, 90}, {294, 160}, {0, 0}}};
    static const Pattern kWarn    = {{{120, 80}, {120, 0},  {0, 0}},
                                     {{220, 180}, {0, 0},   {0, 0}}};
    static const Pattern kTick    = {{{30, 0},   {0, 0},    {0, 0}},
                                     {{2000, 25}, {0, 0},   {0, 0}}};
    static const Pattern kPet     = {{{40, 90},  {30, 90},  {20, 0}},
                                     {{0, 0},    {0, 0},    {0, 0}}};
    static const Pattern kAlert   = {{{180, 100},{100, 0},  {0, 0}},
                                     {{587, 140}, {0, 0},   {0, 0}}};
    static const Pattern kPair    = {{{60, 80},  {60, 0},   {0, 0}},
                                     {{523, 70}, {659, 70}, {784, 120}}};
    static const Pattern kSoft    = {{{40, 0},   {0, 0},    {0, 0}},
                                     {{0, 0},    {0, 0},    {0, 0}}};
    switch (c) {
        case Cue::Ok:    return kOk;
        case Cue::Ng:    return kNg;
        case Cue::Warn:  return kWarn;
        case Cue::Tick:  return kTick;
        case Cue::Pet:   return kPet;
        case Cue::Alert: return kAlert;
        case Cue::Pair:  return kPair;
        default:         return kSoft;
    }
}

/* --------------------------- 音表合成 --------------------------- */

// 每种音型一段静态 PCM。方波占空 50%，首尾 5ms 余弦羽化削爆音

struct Tone {
    std::vector<int16_t> pcm;
    uint16_t ms = 0;
};

Tone s_tones[8][3];   // [cue][note]
Settings s_settings;
int s_rate = 16000;

void synth_tone(Tone& t, const Note& n, uint8_t aud_pct)
{
    t.pcm.clear();
    t.ms = n.ms;
    if (n.freq == 0 || n.ms == 0 || aud_pct == 0) return;

    const int total = static_cast<int>(static_cast<uint32_t>(s_rate) * n.ms / 1000);
    const int edge  = s_rate / 200;  // 5ms
    const int32_t amp = 12000 * aud_pct / 100;
    t.pcm.resize(total);
    for (int i = 0; i < total; i++) {
        const float phase = static_cast<float>(i) * n.freq / s_rate;
        int32_t v = (phase - std::floor(phase)) < 0.5f ? amp : -amp;
        if (i < edge) v = v * i / edge;
        if (i > total - edge) v = v * (total - i) / edge;
        t.pcm[i] = static_cast<int16_t>(v);
    }
}

void synth_all()
{
    for (int c = 0; c < 8; c++) {
        const Pattern& p = pattern_of(static_cast<Cue>(c));
        for (int i = 0; i < 3; i++) synth_tone(s_tones[c][i], p.notes[i], s_settings.aud_pct);
    }
}

/* --------------------------- 播放任务 --------------------------- */

struct Job {
    uint8_t cue;
    bool with_audio;
};

QueueHandle_t s_q = nullptr;

void player_task(void*)
{
    Job j;
    while (xQueueReceive(s_q, &j, portMAX_DELAY) == pdTRUE) {
        const Pattern& p = pattern_of(static_cast<Cue>(j.cue));
        const Tone* tones = s_tones[j.cue];

        for (int i = 0; i < 3; i++) {
            const Pulse& pu = p.pulses[i];
            const Note& no = p.notes[i];
            const bool more_vib = pu.ms > 0;
            const bool more_aud = j.with_audio && no.freq > 0 && no.ms > 0 &&
                                  s_settings.aud_pct > 0;
            if (!more_vib && !more_aud) break;

            if (more_vib) {
                GetHAL().vibrate(pu.ms, static_cast<uint8_t>(s_settings.vib_pct));
            }
            if (more_aud) {
                Tone& t = const_cast<Tone&>(tones[i]);
                if (!t.pcm.empty()) GetHAL().audioPlay(t.pcm, true);
            }

            uint32_t wait = pu.ms + pu.gap;
            if (more_aud && no.ms > wait) wait = no.ms;
            if (wait) vTaskDelay(pdMS_TO_TICKS(wait));
        }

        // 队列里若已堆了新 Cue，立即接续（自然打断：本段已播完）
    }
    vTaskDelete(nullptr);
}

}  // namespace

void init()
{
    s_rate = GetHAL().getAudioSampleRate();
    if (s_rate <= 0) s_rate = 16000;
    synth_all();
    if (!s_q) {
        s_q = xQueueCreate(4, sizeof(Job));
        xTaskCreatePinnedToCore(player_task, "sinan_hap", 3072, nullptr, 3, nullptr, 1);
    }
}

void set(const Settings& s)
{
    s_settings = s;
    synth_all();  // 音量活在音表里，不在播放路径上
}

Settings get() { return s_settings; }

void play(Cue c)
{
    if (!s_q) return;
    Job j{static_cast<uint8_t>(c), true};
    xQueueSend(s_q, &j, 0);  // 满了就丢，提示音不值得排队等
}

void vibe_only(Cue c)
{
    if (!s_q) return;
    Job j{static_cast<uint8_t>(c), false};
    xQueueSend(s_q, &j, 0);
}

}  // namespace sinan::haptics

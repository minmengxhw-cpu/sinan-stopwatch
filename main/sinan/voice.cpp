#include "voice.h"
#include "bridge_ws.h"
#include "state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <atomic>
#include <vector>

namespace sinan::voice {

namespace {

std::atomic<bool> s_running{false};
std::atomic<bool> s_keep{false};
std::atomic<bool> s_aborted{false};
TaskHandle_t s_task = nullptr;

// 每片 240ms。太短则 WS 帧太密挤掉 BLE，太长则说完到出结果的延迟明显
constexpr uint16_t kSliceMs = 240;

void record_task(void*)
{
    ws::send_audio_begin();
    uint32_t seq = 0;
    std::vector<int16_t> slice;

    while (s_keep.load()) {
        slice.clear();
        GetHAL().audioRecord(slice, kSliceMs, 30.0f);
        if (!slice.empty()) ws::send_audio_chunk(slice.data(), slice.size(), seq++);
    }

    if (s_aborted.load()) {
        // 用户中途取消：别发 asr_end（那会让 daemon 转写并把状态推回 Uploading），
        // 改发 asr_cancel 让 daemon 也丢掉这段缓冲。本地状态 abort() 早就置回 Idle 了，
        // 这里绝不能再覆盖一次，否则界面会先 Idle 又跳回"上传中"
        ws::send_audio_cancel();
        s_aborted.store(false);
    } else {
        ws::send_audio_end();
        VoiceState v = State::get().snapshot().voice;
        v.phase = VoicePhase::Uploading;
        State::get().setVoice(v);
    }

    s_running.store(false);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

void begin()
{
    if (s_running.load()) return;

    VoiceState v;
    v.phase = VoicePhase::Recording;
    State::get().setVoice(v);

    s_keep.store(true);
    s_running.store(true);
    // 录音必须离开 UI 线程，否则 audioRecord 的阻塞会把动画卡成幻灯片
    xTaskCreatePinnedToCore(record_task, "sinan_rec", 6144, nullptr, 5, &s_task, 1);
}

void end() { s_keep.store(false); }

void abort()
{
    // 先置 aborted 再落 keep=false，保证 record_task 循环退出时一定能看到 aborted=true
    s_aborted.store(true);
    s_keep.store(false);
    VoiceState v;
    v.phase = VoicePhase::Idle;
    State::get().setVoice(v);
}

}  // namespace sinan::voice

#include "voice.h"
#include "bridge_ws.h"
#include <esp_log.h>
#include "state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal/hal.h>
#include <atomic>
#include <string>
#include <vector>

namespace sinan::voice {

namespace {

std::atomic<bool> s_running{false};
std::atomic<bool> s_keep{false};
std::atomic<bool> s_aborted{false};
TaskHandle_t s_task = nullptr;
std::string s_target;

// 每片 240ms。太短则 WS 帧太密挤掉 BLE，太长则说完到出结果的延迟明显
constexpr uint16_t kSliceMs = 240;

// 短于这个就当误碰，不发。说一个字也要 300ms 以上
constexpr uint32_t kMinRecMs = 280;

std::atomic<uint32_t> s_started{0};
std::atomic<uint32_t> s_elapsed{0};

void record_task(void*)
{
    if (!ws::send_audio_begin(s_target.c_str())) {
        State::get().mutate([](Snapshot& s) {
            s.voice.phase = VoicePhase::Error;
            s.voice.note = "Mac offline";
        });
        s_running.store(false);
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    uint32_t seq = 0;
    std::vector<int16_t> slice;

    while (s_keep.load()) {
        slice.clear();
        GetHAL().audioRecord(slice, kSliceMs, 30.0f);
        if (!slice.empty()) {
            ws::send_audio_chunk(slice.data(), slice.size(), seq++);
        }
    }

    const uint32_t took = GetHAL().millis() - s_started.load();
    s_elapsed.store(took);

    if (s_aborted.load()) {
        // 主动取消必须显式告诉 daemon 丢掉 PCM。若发 asr_end，后台仍会
        // 转写并在稍后把 Ready 写回，用户下次进入 Work 会看到上一段残留。
        ws::send_audio_cancel();
        State::get().mutate([](Snapshot& s) {
            s.voice = VoiceState{};
        });
    } else if (took < kMinRecMs) {
        ws::send_audio_end();
        State::get().mutate([](Snapshot& s) {
            s.voice.phase = VoicePhase::Error;
            s.voice.note  = "too short";
        });
        ESP_LOGI("sinan.voice", "discarded, only %ums", static_cast<unsigned>(took));
    } else {
        // 这里只请求转写。真正交给编程工具要等 Work 页的 B 键确认。
        ws::send_audio_end();
        State::get().mutate([](Snapshot& s) {
            s.voice.phase = VoicePhase::Sending;
            s.voice.note.clear();
        });
    }

    s_running.store(false);
    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

bool begin(const char* target)
{
    if (s_running.load()) return false;
    if (!ws::connected()) {
        State::get().mutate([](Snapshot& s) {
            s.voice = VoiceState{};
            s.voice.phase = VoicePhase::Error;
            s.voice.note = "Mac offline";
        });
        return false;
    }

    s_target = target ? target : "";

    State::get().mutate([](Snapshot& s) {
        s.voice = VoiceState{};
        s.voice.phase = VoicePhase::Recording;
        s.voice.target = s_target;
    });

    s_aborted.store(false);
    s_started.store(GetHAL().millis());
    s_keep.store(true);
    s_running.store(true);
    // 录音必须离开 UI 线程，否则 audioRecord 的阻塞会把动画卡成幻灯片
    xTaskCreatePinnedToCore(record_task, "sinan_rec", 6144, nullptr, 5, &s_task, 1);
    return true;
}

void end() { s_keep.store(false); }   // 录音任务会自己收尾并发送

bool submit(const char* target)
{
    const auto s = State::get().snapshot();
    if (s.voice.phase != VoicePhase::Ready || s.voice.heard.empty() ||
        s.voice.target != (target ? target : "")) return false;
    if (!ws::send_voice(target)) {
        State::get().mutate([](Snapshot& st) {
            st.voice.phase = VoicePhase::Error;
            st.voice.note = "Mac offline";
        });
        return false;
    }
    State::get().mutate([](Snapshot& st) {
        st.voice.phase = VoicePhase::Sending;
        st.voice.note.clear();
    });
    return true;
}

void clear()
{
    if (s_running.load()) return;
    State::get().mutate([](Snapshot& s) { s.voice = VoiceState{}; });
}

bool busy() { return s_running.load(); }

void abort()
{
    s_aborted.store(true);
    s_keep.store(false);
    // 等录音任务真的退出再放行。不等的话，紧接着的 begin() 会跟
    // 还在跑的旧任务抢麦克风，第二次录音直接拿到空数据
    for (int i = 0; i < 40 && s_running.load(); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    State::get().mutate([](Snapshot& s) {
        s.voice = VoiceState{};
    });
}

}  // namespace sinan::voice

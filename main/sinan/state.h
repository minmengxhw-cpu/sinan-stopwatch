/*
 * state.h — 全局状态。
 *
 * 职责边界：网络线程与 UI 线程之间唯一的数据交换点。
 * 网络回调只写这里，应用只读这里的快照。任何一方都不要跨过它直接找对方。
 *
 * 并发模型：FreeRTOS 递归互斥量，不是自旋锁。
 * 早期版本用 portENTER_CRITICAL 保护，而临界区里要拷十几个 std::string ——
 * 关中断期间进堆分配器在 ESP32 上是教科书级的崩法。互斥量允许临界区里分配，
 * 代价是不能在 ISR 里用（我们也没有 ISR 写 State）。
 */
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace sinan {

/* ------------------------------ BLE 侧 ------------------------------ */

struct BleState {
    bool connected      = false;
    uint32_t last_beat  = 0;
    int total           = 0;
    int running         = 0;
    int waiting         = 0;
    uint32_t tokens_today = 0;
    std::string msg;
    std::array<std::string, 3> entries;

    bool has_prompt = false;
    std::string prompt_id;     // 回传时必须原样带回，这是防误批的唯一保险
    std::string prompt_tool;
    std::string prompt_hint;
    uint32_t prompt_since = 0;

    // 已决策但心跳还没跟上的请求。见 State::decide() 的说明
    std::string settled_id;

    std::string owner;
    uint32_t passkey = 0;      // 非 0 表示正在配对，UI 要把它显示出来
};

/* ------------------------------ WS 侧 ------------------------------ */

enum class WorkerState : uint8_t { Idle, Run, Stall, Down };

struct Worker {
    std::string id;
    std::string label;
    std::string task;
    WorkerState state = WorkerState::Down;
    float quota       = 0.0f;
};

struct FleetState {
    static constexpr int kMax = 6;
    std::array<Worker, kMax> workers;
    int count          = 0;
    uint32_t last_recv = 0;
    bool stale         = true;
};

struct AlmanacState {
    std::string number_code;
    std::string number_title;
    std::string huangli_trend;
    std::string huangli_yi;
    std::string huangli_ji;
    int ring_doy = 0;
    std::string ring_tag;
    uint32_t last_recv = 0;
};

/* ------------------------------ 语音侧 ------------------------------ */

enum class VoicePhase : uint8_t {
    Idle, Recording, Sending, Transcribing, Ready, Thinking, Speaking, Error
};

struct VoiceState {
    VoicePhase phase = VoicePhase::Idle;
    std::string target;
    std::string heard;
    std::string reply;
    std::string note;   // 出错时给人看的一行英文，如 "no link" / "too short"
};

struct Tally {
    uint32_t approved = 0;
    uint32_t denied   = 0;
};

struct Snapshot {
    BleState ble;
    FleetState fleet;
    AlmanacState almanac;
    VoiceState voice;
    Tally tally;
    bool wifi_up = false;
    bool ws_up   = false;
};

class State {
public:
    static State& get();

    // 读：拿一份完整拷贝，之后随便用，不用再持锁
    Snapshot snapshot();

    /*
     * 写：在锁内对状态做原地修改。
     *
     * 所有写路径都必须走这个函数，不能"读快照 → 改 → 写回"——
     * 那是跨线程的读改写，会丢更新：你按下批准清掉 prompt 的同时，
     * BLE 任务正拿着批准前的旧副本准备写回，prompt 就诈尸了。
     */
    void mutate(const std::function<void(Snapshot&)>& fn);

    /*
     * 记下一个已决策的请求 id 并立刻清掉 prompt，全程在同一把锁内。
     * settled_id 用来吃掉后面几拍还带着同一个 prompt 的陈旧心跳 ——
     * 桌面端要过一两百毫秒才知道我们批过了。
     */
    void decide(const std::string& id, bool approved);

    void setLink(bool wifi, bool ws);

private:
    State();
    Snapshot _s;
    SemaphoreHandle_t _mtx = nullptr;

    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t m) : _m(m) { xSemaphoreTakeRecursive(_m, portMAX_DELAY); }
        ~Lock() { xSemaphoreGiveRecursive(_m); }
    private:
        SemaphoreHandle_t _m;
    };
};

}  // namespace sinan

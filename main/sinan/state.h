/*
 * state.h — 全局状态。
 *
 * 职责边界：这是网络线程与 UI 线程之间唯一的数据交换点。
 * 网络回调只写这里，应用只读这里的快照。任何一方都不要跨过它直接找对方。
 */
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <array>
#include <cstdint>
#include <string>

namespace sinan {

/* ------------------------------ BLE 侧 ------------------------------ */

struct BleState {
    bool connected      = false;
    uint32_t last_beat  = 0;   // 最后一次心跳的 millis
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

    // DisplayOnly 配对：设备负责把 6 位码显示出来，不显示链路就永远配不上
    bool passkey_pending = false;
    uint32_t passkey = 0;

    std::string owner;
};

/* ------------------------------ WS 侧 ------------------------------ */

enum class WorkerState : uint8_t { Idle, Run, Stall, Down };

struct Worker {
    std::string id;
    std::string label;
    std::string task;
    WorkerState state = WorkerState::Down;
    float quota       = 0.0f;  // 0.0–1.0 剩余比例
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
    std::string huangli_trend;  // "up" / "flat" / "down"
    std::string huangli_yi;
    std::string huangli_ji;
    int ring_doy = 0;           // 年轮当前高亮的年内第几天
    std::string ring_tag;
    uint32_t last_recv = 0;
};

/* ------------------------------ 语音侧 ------------------------------ */

enum class VoicePhase : uint8_t { Idle, Recording, Uploading, Thinking, Speaking };

struct VoiceState {
    VoicePhase phase = VoicePhase::Idle;
    std::string heard;
    std::string reply;
};

/* ------------------------------ 计数 ------------------------------ */

struct Tally {
    uint32_t approved = 0;
    uint32_t denied   = 0;
    uint32_t uptime_s = 0;
};

/* ------------------------------ 中断事件 ------------------------------ */
/* Approval 走 ble.has_prompt（有自己的字段与校验链），这里只承载
   Error / Done 这类「通知型」全屏中断。写入方：bridge_ws / 未来的桥。*/

enum class IrqKind : uint8_t { None, Error, Done };

struct IrqEvent {
    bool active = false;
    IrqKind kind = IrqKind::None;
    std::string id;      // 去重 key 的一部分，来源方给出（task id 等）
    std::string title;   // 大字一行
    std::string body;    // 次级一行
    uint32_t since_ms = 0;
};

struct HidState {
    bool connected = false;
};

struct Snapshot {
    BleState ble;
    FleetState fleet;
    AlmanacState almanac;
    VoiceState voice;
    Tally tally;
    IrqEvent irq;
    HidState hid;
    bool wifi_up = false;
    bool ws_up   = false;
};

class State {
public:
    static State& get();

    // 读：拿一份完整拷贝，之后随便用，不用再持锁
    Snapshot snapshot();

    // 写：只在网络线程调用。临界区里只做赋值，不做 IO、不碰 LVGL
    void withLock(void (*fn)(Snapshot&, void*), void* ctx);

    // 便捷写入器
    void setBle(const BleState& s);
    void clearPrompt();
    void setFleet(const FleetState& s);
    void setAlmanac(const AlmanacState& s);
    void setVoice(const VoiceState& s);
    void setLink(bool wifi, bool ws);
    void bumpApproved();
    void bumpDenied();
    void setIrq(const IrqEvent& e);
    void clearIrq();
    void setHid(bool connected);
    void setPasskey(uint32_t code);
    void clearPasskey();

private:
    State();
    Snapshot _s;
    // 之前用 portMUX_TYPE 自旋锁（关中断），但 Snapshot 里一堆 std::string/array<string>，
    // 逐字段拷贝随时可能触发 malloc/free —— 在关中断区间里做堆分配，耗时不确定，
    // 会拖慢/卡住同核心上的其它中断（含 BLE 协议栈的定时收发）。
    // 全部访问都发生在任务上下文（网络线程写、UI 线程读），不是 ISR，
    // 换成可阻塞的 FreeRTOS 互斥量才是对的：临界区可以安心做堆分配。
    SemaphoreHandle_t _mutex;
};

}  // namespace sinan

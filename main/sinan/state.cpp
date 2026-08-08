#include "state.h"

namespace sinan {

namespace {

// RAII 包装，替代原来手写的 portENTER_CRITICAL/portEXIT_CRITICAL 自旋锁配对，
// 避免某个分支 return 提前跳出忘记 portEXIT_CRITICAL（原实现里虽然没有这个问题，
// 换成 RAII 之后新增分支也不会引入这类锁泄漏）
struct Lock {
    explicit Lock(SemaphoreHandle_t m) : mutex(m) { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
    SemaphoreHandle_t mutex;
};

}  // namespace

State::State() : _mutex(xSemaphoreCreateMutex()) {}

State& State::get()
{
    static State inst;
    return inst;
}

Snapshot State::snapshot()
{
    Lock lock(_mutex);
    return _s;
}

void State::withLock(void (*fn)(Snapshot&, void*), void* ctx)
{
    Lock lock(_mutex);
    fn(_s, ctx);
}

void State::setBle(const BleState& s)
{
    Lock lock(_mutex);
    _s.ble = s;
}

void State::clearPrompt()
{
    Lock lock(_mutex);
    _s.ble.has_prompt = false;
    _s.ble.prompt_id.clear();
    _s.ble.prompt_tool.clear();
    _s.ble.prompt_hint.clear();
}

void State::setFleet(const FleetState& s)
{
    Lock lock(_mutex);
    _s.fleet = s;
}

void State::setAlmanac(const AlmanacState& s)
{
    Lock lock(_mutex);
    _s.almanac = s;
}

void State::setVoice(const VoiceState& s)
{
    Lock lock(_mutex);
    _s.voice = s;
}

void State::setLink(bool wifi, bool ws)
{
    Lock lock(_mutex);
    _s.wifi_up = wifi;
    _s.ws_up   = ws;
}

void State::bumpApproved()
{
    Lock lock(_mutex);
    _s.tally.approved++;
}

void State::bumpDenied()
{
    Lock lock(_mutex);
    _s.tally.denied++;
}

void State::setIrq(const IrqEvent& e)
{
    Lock lock(_mutex);
    _s.irq = e;
}

void State::clearIrq()
{
    Lock lock(_mutex);
    _s.irq.active = false;
}

void State::setHid(bool connected)
{
    Lock lock(_mutex);
    _s.hid.connected = connected;
}

void State::setPasskey(uint32_t code)
{
    Lock lock(_mutex);
    _s.ble.passkey_pending = true;
    _s.ble.passkey = code;
}

void State::clearPasskey()
{
    Lock lock(_mutex);
    _s.ble.passkey_pending = false;
}

}  // namespace sinan

#include "state.h"

namespace sinan {

State& State::get()
{
    static State inst;
    return inst;
}

Snapshot State::snapshot()
{
    Snapshot copy;
    portENTER_CRITICAL(&_mux);
    copy = _s;
    portEXIT_CRITICAL(&_mux);
    return copy;
}

void State::withLock(void (*fn)(Snapshot&, void*), void* ctx)
{
    portENTER_CRITICAL(&_mux);
    fn(_s, ctx);
    portEXIT_CRITICAL(&_mux);
}

void State::setBle(const BleState& s)
{
    portENTER_CRITICAL(&_mux);
    _s.ble = s;
    portEXIT_CRITICAL(&_mux);
}

void State::clearPrompt()
{
    portENTER_CRITICAL(&_mux);
    _s.ble.has_prompt = false;
    _s.ble.prompt_id.clear();
    _s.ble.prompt_tool.clear();
    _s.ble.prompt_hint.clear();
    portEXIT_CRITICAL(&_mux);
}

void State::setFleet(const FleetState& s)
{
    portENTER_CRITICAL(&_mux);
    _s.fleet = s;
    portEXIT_CRITICAL(&_mux);
}

void State::setAlmanac(const AlmanacState& s)
{
    portENTER_CRITICAL(&_mux);
    _s.almanac = s;
    portEXIT_CRITICAL(&_mux);
}

void State::setVoice(const VoiceState& s)
{
    portENTER_CRITICAL(&_mux);
    _s.voice = s;
    portEXIT_CRITICAL(&_mux);
}

void State::setLink(bool wifi, bool ws)
{
    portENTER_CRITICAL(&_mux);
    _s.wifi_up = wifi;
    _s.ws_up   = ws;
    portEXIT_CRITICAL(&_mux);
}

void State::bumpApproved()
{
    portENTER_CRITICAL(&_mux);
    _s.tally.approved++;
    portEXIT_CRITICAL(&_mux);
}

void State::bumpDenied()
{
    portENTER_CRITICAL(&_mux);
    _s.tally.denied++;
    portEXIT_CRITICAL(&_mux);
}

void State::setIrq(const IrqEvent& e)
{
    portENTER_CRITICAL(&_mux);
    _s.irq = e;
    portEXIT_CRITICAL(&_mux);
}

void State::clearIrq()
{
    portENTER_CRITICAL(&_mux);
    _s.irq.active = false;
    portEXIT_CRITICAL(&_mux);
}

void State::setHid(bool connected)
{
    portENTER_CRITICAL(&_mux);
    _s.hid.connected = connected;
    portEXIT_CRITICAL(&_mux);
}

void State::setPasskey(uint32_t code)
{
    portENTER_CRITICAL(&_mux);
    _s.ble.passkey_pending = true;
    _s.ble.passkey = code;
    portEXIT_CRITICAL(&_mux);
}

void State::clearPasskey()
{
    portENTER_CRITICAL(&_mux);
    _s.ble.passkey_pending = false;
    portEXIT_CRITICAL(&_mux);
}

}  // namespace sinan

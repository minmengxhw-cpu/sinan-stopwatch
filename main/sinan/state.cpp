#include "state.h"
#include <functional>

namespace sinan {

State::State()
{
    // 递归的：mutate 里再调 decide 不会自锁
    _mtx = xSemaphoreCreateRecursiveMutex();
}

State& State::get()
{
    static State inst;
    return inst;
}

Snapshot State::snapshot()
{
    Lock lk(_mtx);
    return _s;   // 互斥量下允许堆分配，所以这里拷 std::string 是安全的
}

void State::mutate(const std::function<void(Snapshot&)>& fn)
{
    Lock lk(_mtx);
    fn(_s);
}

void State::decide(const std::string& id, bool approved)
{
    Lock lk(_mtx);
    if (approved) _s.tally.approved++;
    else _s.tally.denied++;

    _s.ble.settled_id = id;      // 后续带同一个 id 的心跳一律忽略
    _s.ble.has_prompt = false;
    _s.ble.prompt_id.clear();
    _s.ble.prompt_tool.clear();
    _s.ble.prompt_hint.clear();
}

void State::setLink(bool wifi, bool ws)
{
    Lock lk(_mtx);
    _s.wifi_up = wifi;
    _s.ws_up   = ws;
}

}  // namespace sinan

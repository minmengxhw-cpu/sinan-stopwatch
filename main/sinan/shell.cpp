/*
 * shell.cpp — 四层壳的总指挥。
 *
 * 每层只是岁差根容器下的一棵子树：切层 = 显隐，不是销毁重建。
 * 所以审批退回来时团团的照片还在 PSRAM 里，不会重新解码。
 *
 * 按键事件统一在这里采样（wasClicked 一个帧只能有一个读者），
 * 再按 input_map.h 路由给当前层。层不允许自己读按键。
 */
#include "shell.h"
#include "beads.h"
#include "design.h"
#include "gaze_fsm.h"
#include "haptics.h"
#include "input_map.h"
#include "layer_action.h"
#include "layer_interrupt.h"
#include "layer_rest.h"
#include "layer_work.h"
#include "precession.h"
#include "talk_overlay.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <cmath>

namespace sinan::shell {

using namespace design;

namespace {

constexpr uint32_t kWorkIdleMs   = 60000;   // Work 无操作退回 Rest
constexpr uint32_t kActionIdleMs = 12000;   // Action 无操作退回 Work
constexpr uint32_t kIrqDedupeMs  = 45000;   // 同 key 通知 45s 内不重复抢占
constexpr uint32_t kHearHoldMs   = 2500;    // Hear 提示停留时长

Layer s_layer = Layer::Rest;
Layer s_return_layer = Layer::Rest;   // 中断前的层
uint32_t s_idle_since = 0;

int s_settings_id = -1;
int s_work_focus = 0;

// 通知去重
uint32_t s_last_irq_hash = 0;
uint32_t s_last_irq_at   = 0;

// Hear 触发：链路有新事件但未进中断
uint32_t s_last_fleet_seen = 0;
uint32_t s_hear_at = 0;

// 按键沿检测（pressedFor 是电平的，沿要自己造）
bool s_a_long_fired = false;
bool s_b_long_fired = false;
uint32_t s_combo_start = 0;
bool s_combo_latched = false;
bool s_suppress_clicks = false;   // 组合键放过之后，松开不许再当单击

// 触摸手势
bool s_touch_down = false;
bool s_touch_rim = false;
float s_touch_last_deg = 0.0f;
float s_slide_acc = 0.0f;         // 累计角位移（度）
uint32_t s_touch_at = 0;
uint32_t s_touch_move_at = 0;
bool s_talk_started = false;

// debug 注入队列（深度 8，写方是串口任务）
struct Inject { uint8_t kind; int16_t x; int16_t y; };  // kind: 见 inject_key/inject_touch
constexpr int kInjectCap = 8;
Inject s_inject[kInjectCap];
int s_inject_head = 0, s_inject_tail = 0;
portMUX_TYPE s_inject_mux = portMUX_INITIALIZER_UNLOCKED;

void inject_push(const Inject& j)
{
    portENTER_CRITICAL(&s_inject_mux);
    const int next = (s_inject_head + 1) % kInjectCap;
    if (next != s_inject_tail) {
        s_inject[s_inject_head] = j;
        s_inject_head = next;
    }
    portEXIT_CRITICAL(&s_inject_mux);
}

bool inject_pop(Inject& j)
{
    portENTER_CRITICAL(&s_inject_mux);
    if (s_inject_tail == s_inject_head) {
        portEXIT_CRITICAL(&s_inject_mux);
        return false;
    }
    j = s_inject[s_inject_tail];
    s_inject_tail = (s_inject_tail + 1) % kInjectCap;
    portEXIT_CRITICAL(&s_inject_mux);
    return true;
}

bool inject_empty()
{
    portENTER_CRITICAL(&s_inject_mux);
    const bool e = s_inject_tail == s_inject_head;
    portEXIT_CRITICAL(&s_inject_mux);
    return e;
}

/* --------------------------- 层显隐 --------------------------- */

void enter_layer(Layer l, uint32_t now)
{
    switch (l) {
        case Layer::Rest:      layer_rest::enter();      break;
        case Layer::Work:      layer_work::enter();      break;
        case Layer::Action:    layer_action::enter();    break;
        case Layer::Interrupt: layer_interrupt::enter(); break;
    }
    s_idle_since = now;
}

void exit_layer(Layer l)
{
    switch (l) {
        case Layer::Rest:      layer_rest::exit();      break;
        case Layer::Work:      layer_work::exit();      break;
        case Layer::Action:    layer_action::exit();    break;
        case Layer::Interrupt: layer_interrupt::exit(); break;
    }
}

void switch_to(Layer l, uint32_t now)
{
    if (l == s_layer) return;
    mclog::tagInfo("Shell", "layer {} -> {}", static_cast<int>(s_layer), static_cast<int>(l));
    exit_layer(s_layer);
    s_layer = l;
    enter_layer(l, now);
}

/* --------------------------- 中断编排 --------------------------- */

uint32_t irq_hash(const IrqEvent& e)
{
    // kind + id 的 FNV-1a。45 秒内同 key 不重复抢占
    uint32_t h = 2166136261u;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(e.id.c_str());
    for (size_t i = 0; i < e.id.size(); i++) { h ^= p[i]; h *= 16777619u; }
    h ^= static_cast<uint8_t>(e.kind);
    return h;
}

// 现在该不该有中断在台上？优先级：审批 > 配对 > 通知
bool interrupt_wanted(const Snapshot& s, uint32_t now)
{
    if (s.ble.has_prompt) return true;
    if (s.ble.passkey_pending) return true;
    if (s.irq.active) {
        // Grave 审批在台上时 Done 不许打断；去重窗口内同 key 直接吞掉
        const uint32_t h = irq_hash(s.irq);
        if (h == s_last_irq_hash && now - s_last_irq_at < kIrqDedupeMs) {
            State::get().clearIrq();   // 吞掉要清干净，不然它会赖在 State 里
            return false;
        }
        s_last_irq_hash = h;
        s_last_irq_at = now;
        return true;
    }
    return false;
}

void orchestrate_interrupt(const Snapshot& s, uint32_t now)
{
    if (s_layer != Layer::Interrupt) {
        if (!interrupt_wanted(s, now)) return;
        if (talk::active()) talk::abort();   // 审批比说话重要
        s_return_layer = s_layer;
        gaze::notify(gaze::Mood::Alert, "interrupt", now);
        switch_to(Layer::Interrupt, now);
        return;
    }

    // 在台上：让给层自己演（换脸、倒计时、落定），演完了收台
    if (layer_interrupt::tick(now)) {
        const Layer back = s_return_layer;
        switch_to(back, now);
        if (back == Layer::Rest) {
            // 回到团团身边。批准过的决定由 on_decision_made 换成 Praise
            if (gaze::mood() == gaze::Mood::Alert || gaze::mood() == gaze::Mood::Guard) {
                gaze::notify(gaze::Mood::Sleep, "interrupt over", now);
            }
        }
    }
}

/* --------------------------- 输入路由 --------------------------- */

void open_settings()
{
    if (s_settings_id < 0) return;
    haptics::play(haptics::Cue::Soft);
    mooncake::GetMooncake().openApp(s_settings_id);
}

void route_key(Key k, uint32_t now)
{
    s_idle_since = now;
    gaze::poke(now);

    switch (s_layer) {
        case Layer::Rest:
            layer_rest::key(k);
            if (k == Key::BLong) open_settings();
            break;
        case Layer::Work:
            if (!layer_work::key(k) && k == Key::BLong) switch_to(Layer::Rest, now);
            break;
        case Layer::Action:
            if (!layer_action::key(k) && k == Key::BLong) switch_to(Layer::Work, now);
            break;
        case Layer::Interrupt:
            layer_interrupt::key(k);
            break;
    }
}

void route_combo(uint32_t now)
{
    s_idle_since = now;
    gaze::poke(now);
    haptics::play(haptics::Cue::Tick);
    switch (s_layer) {
        case Layer::Rest:   switch_to(Layer::Work, now);   break;
        case Layer::Work:   switch_to(Layer::Action, now); break;
        case Layer::Action: switch_to(Layer::Work, now);   break;
        case Layer::Interrupt: break;  // 审批台上不许切层
    }
}

void poll_buttons(uint32_t now)
{
    auto& hal = GetHAL();
    hal.updateButtonStates();

    const bool a = hal.btnA.isPressed();
    const bool b = hal.btnB.isPressed();

    // A+B 组合优先：300ms 同按算层切换，一握只发一次
    if (a && b) {
        if (s_combo_start == 0) s_combo_start = now;
        if (!s_combo_latched && now - s_combo_start >= input_map::COMBO_MS) {
            s_combo_latched = true;
            s_suppress_clicks = true;
            route_combo(now);
        }
        // 组合期间压掉单键长按
        s_a_long_fired = true;
        s_b_long_fired = true;
        return;
    }
    s_combo_start = 0;

    if (!a && !b) {
        s_combo_latched = false;
        s_suppress_clicks = false;
        s_a_long_fired = false;
        s_b_long_fired = false;
        return;
    }

    if (!s_suppress_clicks) {
        if (hal.btnA.wasClicked()) route_key(Key::AShort, now);
        if (hal.btnB.wasClicked()) route_key(Key::BShort, now);
    }

    if (a && !s_a_long_fired && hal.btnA.pressedFor(LONGPRESS_MS)) {
        s_a_long_fired = true;
        route_key(Key::ALong, now);
    }
    if (b && !s_b_long_fired && hal.btnB.pressedFor(LONGPRESS_MS)) {
        s_b_long_fired = true;
        route_key(Key::BLong, now);
    }
}

/* --------------------------- 触摸手势 --------------------------- */

float polar_deg(int x, int y)
{
    const float dx = static_cast<float>(x - CENTER);
    const float dy = static_cast<float>(y - CENTER);
    float deg = std::atan2(dx, -dy) * 180.0f / 3.14159265358979f;  // 12 点为 0°，顺时针
    if (deg < 0) deg += 360.0f;
    return deg;
}

void poll_touch(uint32_t now)
{
    const auto tp = GetHAL().getTouchPoint();
    const bool down = tp.num > 0;

    if (down && !s_touch_down) {
        // 落指：判区域。Rim 带 = 盘珠/抚触，中心 = Peek/对讲
        s_touch_down = true;
        s_touch_at = now;
        s_touch_move_at = now;
        s_slide_acc = 0.0f;
        s_talk_started = false;
        s_touch_last_deg = polar_deg(tp.x, tp.y);
        const float dx = tp.x - CENTER, dy = tp.y - CENTER;
        const float r = std::sqrt(dx * dx + dy * dy);
        s_touch_rim = (r > R_ORBIT_IN);

        // B 按住 + 点屏 = 换照片（Rest 专属）
        if (s_layer == Layer::Rest && GetHAL().btnB.isPressed()) {
            layer_rest::next_photo();
            s_touch_down = false;  // 这次触摸已消费
            return;
        }
        gaze::poke(now);
        return;
    }

    if (down && s_touch_down) {
        const float deg = polar_deg(tp.x, tp.y);
        float d = deg - s_touch_last_deg;
        if (d > 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        s_touch_last_deg = deg;

        const float dx = tp.x - CENTER, dy = tp.y - CENTER;
        const float r = std::sqrt(dx * dx + dy * dy);

        // 中心长按 = 对讲（任何非中断层）
        if (!s_touch_rim && r < input_map::CENTER_TAP_R && s_layer != Layer::Interrupt) {
            if (!s_talk_started && now - s_touch_at >= input_map::TALK_HOLD_MS) {
                s_talk_started = true;
                talk::begin(now);
            }
            return;
        }

        // Rim 滑动：角速度分快慢。快 = 盘珠/切 agent，慢 = 抚触
        if (s_touch_rim || r > R_ORBIT_IN) {
            s_slide_acc += d;
            const float v = std::fabs(d) / std::max<uint32_t>(now - s_touch_move_at, 1) * 1000.0f;
            s_touch_move_at = now;
            const bool fast = v > input_map::SLIDE_FAST_DPS;

            if (std::fabs(s_slide_acc) >= input_map::BEAD_STEP_DEG) {
                const float step = s_slide_acc > 0 ? 1.0f : -1.0f;
                s_slide_acc = 0.0f;
                s_idle_since = now;
                switch (s_layer) {
                    case Layer::Rest: layer_rest::rim_slide(step, fast, now); break;
                    case Layer::Work: layer_work::rim_slide(step, fast, now); break;
                    default: break;
                }
            } else if (!fast && s_layer == Layer::Rest && std::fabs(s_slide_acc) > 6.0f) {
                // 慢而持续 = 抚触（一次手势只呼噜一回）
                static uint32_t s_last_pet = 0;
                if (now - s_last_pet > 3000) {
                    s_last_pet = now;
                    layer_rest::rim_slide(0.0f, false, now);
                }
            }
        }
        return;
    }

    if (!down && s_touch_down) {
        // 抬指：短触中心 = Peek/选中/触发
        s_touch_down = false;
        if (s_talk_started) {
            talk::end();
            return;
        }
        const float dx = tp.x - CENTER, dy = tp.y - CENTER;
        const float r = std::sqrt(dx * dx + dy * dy);
        const uint32_t held = now - s_touch_at;
        if (held < input_map::TALK_HOLD_MS && std::fabs(s_slide_acc) < 8.0f) {
            s_idle_since = now;
            switch (s_layer) {
                case Layer::Rest:
                    if (r < input_map::CENTER_TAP_R) layer_rest::tap_center(now);
                    break;
                case Layer::Work:
                    if (r < input_map::CENTER_TAP_R) layer_work::tap_center(now);
                    break;
                case Layer::Action:
                    layer_action::tap_polar(polar_deg(tp.x, tp.y), r, now);
                    break;
                default: break;
            }
        }
    }
}

/* --------------------------- 敲击 --------------------------- */

void poll_knock(uint32_t now)
{
    auto& hal = GetHAL();
    hal.updateImuData();
    const auto& imu = hal.getImuData();
    const float g = std::sqrt(imu.accelX * imu.accelX + imu.accelY * imu.accelY +
                              imu.accelZ * imu.accelZ);
    static uint32_t s_last_knock = 0;
    if (g > TAP_G && now - s_last_knock > 1500) {
        s_last_knock = now;
        gaze::poke(now);
        if (s_layer == Layer::Rest) layer_rest::imu_tap(now);
        else if (s_layer != Layer::Interrupt) ui::set_luma(ui::Luma::Normal);
    }
}

/* --------------------------- Hear：有动静但不必抢屏 --------------------------- */

void poll_hear(const Snapshot& s, uint32_t now)
{
    static bool s_was_connected = false;

    if (s_layer != Layer::Rest) {
        s_last_fleet_seen = s.fleet.last_recv;
        s_was_connected = s.ble.connected;
        return;
    }
    // Fleet 推送（20s 一拍）或 BLE 重连才算「新闻」；心跳每秒都有，不算
    const bool fleet_news = s.fleet.last_recv != s_last_fleet_seen && s_last_fleet_seen != 0;
    const bool reconnected = s.ble.connected && !s_was_connected;
    s_last_fleet_seen = s.fleet.last_recv;
    s_was_connected = s.ble.connected;

    if ((fleet_news || reconnected) && gaze::mood() == gaze::Mood::Sleep) {
        gaze::notify(gaze::Mood::Hear, fleet_news ? "fleet event" : "ble reconnect", now);
        s_hear_at = now;
    }
    if (gaze::mood() == gaze::Mood::Hear && now - s_hear_at > kHearHoldMs) {
        gaze::notify(gaze::Mood::Sleep, "hear done", now);
    }
}

/* --------------------------- 注入回放 --------------------------- */

void replay_inject(uint32_t now)
{
    // 触摸注入：'t' 带坐标为落指，y=-1 为抬指。点按语义与 poll_touch 抬起分支一致
    static int s_ij_x = 0, s_ij_y = 0;

    Inject j;
    while (inject_pop(j)) {
        if (j.kind == 't') {
            if (j.y >= 0) {
                s_ij_x = j.x;
                s_ij_y = j.y;
            } else {
                const float dx = s_ij_x - CENTER, dy = s_ij_y - CENTER;
                const float r = std::sqrt(dx * dx + dy * dy);
                switch (s_layer) {
                    case Layer::Rest:
                        if (r < input_map::CENTER_TAP_R) layer_rest::tap_center(now);
                        break;
                    case Layer::Work:
                        if (r < input_map::CENTER_TAP_R) layer_work::tap_center(now);
                        break;
                    case Layer::Action:
                        layer_action::tap_polar(polar_deg(s_ij_x, s_ij_y), r, now);
                        break;
                    default: break;
                }
            }
            continue;
        }
        switch (j.kind) {
            case 'a': route_key(Key::AShort, now); break;
            case 'b': route_key(Key::BShort, now); break;
            case 'A': route_key(Key::ALong, now);  break;
            case 'B': route_key(Key::BLong, now);  break;
            case 'C': route_combo(now);            break;
            case 'L':
                if (s_layer != Layer::Interrupt && j.x <= static_cast<int>(Layer::Action)) {
                    switch_to(static_cast<Layer>(j.x), now);
                }
                break;
            default: break;
        }
    }
}

}  // namespace

void init()
{
    // 层只建一次。先建的在下，后建的在上 —— Interrupt 永远压所有层
    layer_rest::create();
    layer_work::create();
    layer_action::create();
    talk::create();
    layer_interrupt::create();

    const uint32_t now = GetHAL().millis();
    gaze::init(now);
    s_layer = Layer::Rest;
    layer_rest::enter();
    mclog::tagInfo("Shell", "init, boot to Rest");
}

void tick(uint32_t now)
{
    // Settings 在台上时，输入与层演出全部让位（它自己读按键）
    if (s_settings_id >= 0 &&
        mooncake::GetMooncake().getAppCurrentState(s_settings_id) ==
            mooncake::AppAbility::StateRunning) {
        return;
    }

    const auto snap = State::get().snapshot();

    gaze::tick(now);
    talk::tick(now);
    orchestrate_interrupt(snap, now);

    // 注入优先：脚本调试不等物理按键
    if (!inject_empty()) replay_inject(now);

    if (!talk::active()) {
        poll_buttons(now);
        poll_touch(now);
        poll_knock(now);
    } else {
        // 对讲中只认 B = 挂断
        GetHAL().updateButtonStates();
        if (GetHAL().btnB.wasClicked()) talk::abort();
    }

    // 当前层的逐帧
    switch (s_layer) {
        case Layer::Rest:      layer_rest::tick(now);      break;
        case Layer::Work:      layer_work::tick(now);      break;
        case Layer::Action:    layer_action::tick(now);    break;
        case Layer::Interrupt: break;  // 已在 orchestrate 里 tick 过
    }

    poll_hear(snap, now);

    // 无操作回落
    if (s_layer == Layer::Action && now - s_idle_since > kActionIdleMs) {
        switch_to(Layer::Work, now);
    } else if (s_layer == Layer::Work && now - s_idle_since > kWorkIdleMs) {
        switch_to(Layer::Rest, now);
    }
}

Layer current() { return s_layer; }

void set_settings_app_id(int id) { s_settings_id = id; }

int work_focus() { return s_work_focus; }

void set_work_focus(int i) { s_work_focus = i; }

void inject_key(char k)
{
    inject_push(Inject{static_cast<uint8_t>(k), 0, 0});
}

void inject_touch(int x, int y, bool down)
{
    inject_push(Inject{'t', static_cast<int16_t>(x), static_cast<int16_t>(down ? y : -1)});
}

void inject_layer(int l)
{
    inject_push(Inject{'L', static_cast<int16_t>(l), 0});
}

void on_decision_made(bool approved, uint32_t now)
{
    // 批准过的决定回到 Rest 是一声石绿的回响；被拒绝也是因果，上珠串
    if (approved) gaze::notify(gaze::Mood::Praise, "approved", now);
    beads::rebuild_from(State::get().snapshot());
}

}  // namespace sinan::shell

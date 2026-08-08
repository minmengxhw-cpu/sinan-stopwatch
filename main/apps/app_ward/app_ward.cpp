/*
 * app_ward.cpp — 守。
 *
 * 三种形态，共用同一圈 Rim 环：
 *   静默  纯黑，Rim 只剩一段靛青短弧在缓慢呼吸。AMOLED 上这几乎不耗电，
 *         也不会烧屏，可以就这样在桌上摆一整天。
 *   待决  Rim 整圈点亮并按剩余时间收缩，颜色由危险等级决定。
 *   落定  弧从中线向两侧张开成整圈，石绿或朱砂，0.48 秒后回到静默。
 *
 * 所有过渡都是弧的生长与退让，没有一处滑入滑出。
 */
#include "app_ward.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/state.h>
#include <sinan/bridge_ble.h>
#include <sinan/danger.h>
#include <sinan/route.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <algorithm>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;

namespace {

// 一个请求给 90 秒。到时不自动批准也不自动拒绝，只是收起催促，
// 让它回到静默 —— 超时替用户做决定是不可接受的
constexpr uint32_t kPromptWindowMs = 90000;

enum class Face { Quiet, Pending, Settled, Pairing };

}  // namespace

AppWard::AppWard()
{
    static uint32_t launcher_color = INDIGO;
    setAppInfo().name = "Buddy";
    setAppInfo().icon = (void*)&icon_approval;
    setAppInfo().userData = &launcher_color;
}

void AppWard::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppWard::onOpen()
{
    _key = std::make_unique<input::KeyManager>(true);
    _face = static_cast<int>(Face::Quiet);
    _shown_id.clear();
    _settled_at = 0;

    LvglLockGuard lock;
    ui::root_acquire();   // 必须在持锁状态下调，见 precession.h 的锁契约
    _stage = ui::stage(ui::precession_root());

    // Rim：唯一一件两米外看得清的东西
    _rim = ui::arc(_stage, {R_RIM, W_RIM, INDIGO, LACQUER, true}, 0, 40);

    // Orbit：待决时显示危险等级的刻度，静默时熄灭
    _orbit = ui::ticks(_stage, R_ORBIT, 36, 14, 3, LACQUER);

    // Core：工具名（大字）+ 命令内容（等宽，可折行）
    _tool = ui::text(_stage, "", SN_FONT_MONO_L, SILK);
    lv_obj_align(_tool, LV_ALIGN_CENTER, 0, -62);

    _hint = ui::mono_block(_stage, "", SN_FONT_MONO_S, SILK_D, 250);
    lv_obj_align(_hint, LV_ALIGN_CENTER, 0, 6);

    _reason = ui::text(_stage, "", SN_FONT_MONO_S, CINNABAR);
    lv_obj_align(_reason, LV_ALIGN_CENTER, 0, 82);

    // 静默态中心：一枚司南浮针的意象，只是一个小圆点加一道短线
    _needle = lv_obj_create(_stage);
    lv_obj_set_size(_needle, 8, 8);
    lv_obj_set_style_radius(_needle, 4, 0);
    lv_obj_set_style_bg_color(_needle, c(BRONZE_D), 0);
    lv_obj_set_style_border_width(_needle, 0, 0);
    lv_obj_center(_needle);

    build_glyph();
    // 配对码：DisplayOnly 配对时唯一能让人完成配对的东西
    _passkey = ui::text(_stage, "", SN_FONT_NUM_XL, SILK);
    lv_obj_center(_passkey);
    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);

    _chord = ui::chord(_stage, "");

    apply_quiet();
}

/*
 * 团团点阵字形：两层同一张 RGBA PNG。
 * 底层是暗鎏金的幽灵，始终在；上层按状态染色，装在一个高度会变的
 * 裁剪容器里 —— 长按时容器从下往上长，看起来就是团团在被填满。
 *
 * 用 recolor 而不是塞一个 68KB 的 C 数组：图是 RGB 全白 + alpha 存点的浓度，
 * 染色只动 RGB，点的疏密原样保留。而且换照片时字形跟着一起换。
 */
void AppWard::build_glyph()
{
    static constexpr int GS = 264;
    const char* path = "A:/spiflash/tuan/glyph.png";

    _ghost = lv_image_create(_stage);
    lv_image_set_src(_ghost, path);
    lv_obj_center(_ghost);
    lv_obj_set_style_image_recolor(_ghost, c(BRONZE_D), 0);
    lv_obj_set_style_image_recolor_opa(_ghost, LV_OPA_COVER, 0);
    // 40% 而不是 26%：静默态屏幕亮度只有 25%，两者一乘就几乎看不见了。
    // 这个值要在真机上按环境光复核
    lv_obj_set_style_opa(_ghost, 102, 0);          // 40%
    lv_obj_add_flag(_ghost, LV_OBJ_FLAG_HIDDEN);

    _clip = lv_obj_create(_stage);
    lv_obj_set_size(_clip, GS, GS);
    lv_obj_center(_clip);
    lv_obj_set_style_bg_opa(_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_clip, 0, 0);
    lv_obj_set_style_pad_all(_clip, 0, 0);
    lv_obj_set_style_clip_corner(_clip, false, 0);
    lv_obj_remove_flag(_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_clip, LV_OBJ_FLAG_HIDDEN);

    _fill = lv_image_create(_clip);
    lv_image_set_src(_fill, path);
    lv_image_set_inner_align(_fill, LV_IMAGE_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_image_recolor_opa(_fill, LV_OPA_COVER, 0);
    lv_obj_set_size(_fill, GS, GS);
    lv_obj_align(_fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// fill 是 0..1 的填充比例，从下往上
void AppWard::set_glyph(bool visible, float fill, uint32_t hue)
{
    static constexpr int GS = 264;
    if (!_ghost || !_clip) return;

    if (!visible) {
        lv_obj_add_flag(_ghost, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_clip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(_ghost, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_clip, LV_OBJ_FLAG_HIDDEN);

    const int h = static_cast<int>(GS * std::clamp(fill, 0.0f, 1.0f));
    lv_obj_set_height(_clip, h > 0 ? h : 1);
    lv_obj_align(_clip, LV_ALIGN_CENTER, 0, (GS - h) / 2);
    lv_obj_set_style_image_recolor(_fill, c(hue), 0);
}

void AppWard::apply_quiet()
{
    _face = static_cast<int>(Face::Quiet);
    ui::set_luma(ui::Luma::Quiet);
    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);

    ui::arc_set_color(_rim, INDIGO);
    ui::arc_animate_to(_rim, 0, 40, T_STATE);
    ui::breathe(_rim, T_BREATH * 2, 40, 170);
    ui::ticks_reset_color(_orbit, LACQUER);

    lv_label_set_text(_tool, "BUDDY");
    lv_obj_set_style_text_color(_tool, c(BRONZE), 0);
    lv_label_set_text(_hint, "BLE PERMISSION GATE");
    lv_label_set_text(_reason, "");
    // 静默态中心不是一颗光秃秃的点，是团团的幽灵。反正这一屏没别的信息
    lv_obj_add_flag(_needle, LV_OBJ_FLAG_HIDDEN);
    set_glyph(true, 0.0f, BRONZE_D);
    lv_label_set_text(_chord, "waiting for Mac");
    lv_obj_set_style_text_color(_chord, c(INDIGO), 0);
}

/*
 * 配对页。macOS 弹窗要你输一个六位码，而这台设备平时不接串口 ——
 * 码不上屏就配不上对，守整条线是死的。所以它优先级高于一切其他形态。
 */
void AppWard::apply_pairing(uint32_t code)
{
    _face = static_cast<int>(Face::Pairing);
    ui::set_luma(ui::Luma::Alert);

    ui::stop_breathe(_rim);
    ui::arc_set_color(_rim, INDIGO);
    ui::arc_set_range(_rim, 0, 359.9f);
    ui::ticks_reset_color(_orbit, LACQUER);
    set_glyph(false, 0.0f, 0);

    lv_obj_add_flag(_needle, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_tool, "");
    lv_label_set_text(_hint, "");
    lv_label_set_text(_reason, "");

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(code % 1000000));
    lv_obj_remove_flag(_passkey, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_passkey, buf);

    lv_label_set_text(_chord, "pair on Mac");
    lv_obj_set_style_text_color(_chord, c(INDIGO), 0);
}

void AppWard::apply_pending(const Snapshot& s)
{
    _face = static_cast<int>(Face::Pending);
    _shown_id = s.ble.prompt_id;
    ui::set_luma(ui::Luma::Alert);
    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);

    const Risk risk = assess(s.ble.prompt_tool, s.ble.prompt_hint);
    _grave = (risk == Risk::Grave);
    const uint32_t hue = _grave ? CINNABAR : (risk == Risk::Elevated ? AMBER : MALACHITE);

    ui::stop_breathe(_rim);
    ui::arc_set_color(_rim, hue);
    ui::arc_set_range(_rim, 0, 359.9f);

    // Orbit 刻度按危险等级点亮的密度不同：越危险，环上越"实"
    const int lit = _grave ? 36 : (risk == Risk::Elevated ? 18 : 9);
    ui::ticks_reset_color(_orbit, LACQUER);
    for (int i = 0; i < 36; i += (36 / lit)) {
        // 底部 60° 留空给弦区那行提示，否则刻度会从文字里穿过去。
        // 环在底部开一个口，看起来是有意的，实际是被文字逼出来的
        if (i >= 15 && i <= 21) continue;
        ui::tick_set_color(_orbit, i, hue);
    }

    lv_obj_add_flag(_needle, LV_OBJ_FLAG_HIDDEN);
    // 读命令的时候屏幕全让给文字，字形退场
    set_glyph(false, 0.0f, 0);
    lv_label_set_text(_tool, s.ble.prompt_tool.c_str());
    lv_obj_set_style_text_color(_tool, c(hue), 0);
    lv_label_set_text(_hint, s.ble.prompt_hint.c_str());

    const char* why = risk_reason(s.ble.prompt_tool, s.ble.prompt_hint);
    lv_label_set_text(_reason, why);
    lv_obj_set_style_text_color(_reason, c(hue), 0);

    // 危险操作明说需要长按。UI 不该让人猜为什么按了没反应
    // 弦区在 y=372，能用的宽度只有 ~310px。文案超过 24 字符会顶出安全圆
    lv_label_set_text(_chord, _grave ? "hold A \xc2\xb7 deny B" : "A allow \xc2\xb7 B deny");
    lv_obj_set_style_text_color(_chord, c(_grave ? CINNABAR : BRONZE), 0);
}

void AppWard::apply_settled(bool approved)
{
    _face = static_cast<int>(Face::Settled);
    _settled_at = GetHAL().millis();
    _shown_id.clear();

    lv_obj_add_flag(_passkey, LV_OBJ_FLAG_HIDDEN);
    ui::stop_breathe(_rim);
    // 从 12 点向两侧张开。bloom 现在真的会用 from_deg
    ui::bloom(_rim, 0, approved ? MALACHITE : CINNABAR, T_STATE);
    ui::ticks_reset_color(_orbit, approved ? MALACHITE : CINNABAR);
    // 批准的终态是团团被石绿填满 —— 跟长按过程的最后一帧接上，
    // 手指松开时画面不跳。拒绝就不留他，直接朱砂弧
    set_glyph(approved, 1.0f, MALACHITE);

    lv_label_set_text(_tool, approved ? "ALLOWED" : "DENIED");
    lv_obj_set_style_text_color(_tool, c(approved ? MALACHITE : CINNABAR), 0);
    lv_label_set_text(_hint, "");
    lv_label_set_text(_reason, "");
    lv_label_set_text(_chord, "");
    _hold_start = 0;

    GetHAL().vibrate(approved ? 90 : 160, 100);
}

void AppWard::onRunning()
{
    auto& hal = GetHAL();
    hal.updateButtonStates();
    if (_key && _key->update(false) == input::KeyEvent::GoHome) {
        close();
        return;
    }

    const uint32_t now = hal.millis();
    const auto s = State::get().snapshot();

    LvglLockGuard lock;

    // 配对压倒一切：配不上对，后面所有形态都没有意义
    if (s.ble.passkey != 0) {
        if (_face != static_cast<int>(Face::Pairing)) apply_pairing(s.ble.passkey);
        return;
    }
    if (_face == static_cast<int>(Face::Pairing)) {
        // 配对有结果了。当初是被配对拉过来的，就回待机页去
        apply_quiet();
        sinan::route::return_home_if_auto();
        return;
    }

    // 落定态只停留一瞬，然后自己退回静默
    if (_face == static_cast<int>(Face::Settled)) {
        if (now - _settled_at > T_STATE + 700) {
            apply_quiet();
            // 当初是被请求自动拉起来的，处理完就回待机页去
            sinan::route::return_home_if_auto();
        }
        return;
    }

    const bool link_dead = !s.ble.connected || (now - s.ble.last_beat > STALE_MS);

    if (link_dead) {
        if (_face != static_cast<int>(Face::Quiet)) apply_quiet();
        // 链路断了要让人看得出来，但不能弹窗。改成弦区一行小字
        lv_label_set_text(_tool, "BUDDY");
        lv_obj_set_style_text_color(_tool, c(BRONZE), 0);
        lv_label_set_text(_hint, "BLE PERMISSION GATE");
        lv_label_set_text(_chord, "waiting for Mac");
        lv_obj_set_style_text_color(_chord, c(INDIGO), 0);
        return;
    }

    if (s.ble.has_prompt) {
        if (_shown_id != s.ble.prompt_id) {
            apply_pending(s);
        } else {
            // Rim 按剩余时间收缩。时间本身就是弧长，不需要额外的进度条
            const uint32_t elapsed = now - s.ble.prompt_since;
            const float left = elapsed >= kPromptWindowMs
                                   ? 0.0f
                                   : 1.0f - static_cast<float>(elapsed) / kPromptWindowMs;
            ui::arc_set_progress(_rim, left);
            handle_keys(s);
        }
        return;
    }

    // prompt 消失了：可能在别处批了，也可能超时。回静默，什么都不声张
    if (_face != static_cast<int>(Face::Quiet)) apply_quiet();

    lv_label_set_text(_tool, "READY");
    lv_obj_set_style_text_color(_tool, c(MALACHITE), 0);
    lv_label_set_text(_hint, "permission inbox");

    char buf[48];
    if (s.ble.total == 0) {
        std::snprintf(buf, sizeof(buf), "idle");
    } else {
        std::snprintf(buf, sizeof(buf), "%d running / %d open", s.ble.running, s.ble.total);
    }
    lv_label_set_text(_chord, buf);
    lv_obj_set_style_text_color(_chord, c(BRONZE_D), 0);
}

void AppWard::handle_keys(const Snapshot& s)
{
    auto& hal = GetHAL();

    if (hal.btnB.wasClicked()) {
        if (ble::send_permission(s.ble.prompt_id, ble::Decision::Deny)) apply_settled(false);
        return;
    }

    if (_grave) {
        // 危险操作只认长按。这 0.8 秒是唯一挡在不可逆操作前面的东西。
        // 一旦手指按下去，屏幕就不再是"读命令"而是"看确认"：
        // 文字退场，团团从下往上被石绿填满，填满即放行。
        // 这样那 0.8 秒是一件正在发生的事，而不是一段没反应的延迟
        if (hal.btnA.pressedFor(LONGPRESS_MS)) {
            if (ble::send_permission(s.ble.prompt_id, ble::Decision::Once)) apply_settled(true);
        } else if (hal.btnA.isPressed()) {
            if (_hold_start == 0) _hold_start = hal.millis();
            const float p = std::clamp(
                static_cast<float>(hal.millis() - _hold_start) / LONGPRESS_MS, 0.0f, 1.0f);
            lv_obj_set_style_opa(_tool, LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(_hint, LV_OPA_TRANSP, 0);
            lv_obj_set_style_opa(_reason, LV_OPA_TRANSP, 0);
            set_glyph(true, p, MALACHITE);
            ui::arc_set_color(_rim, MALACHITE);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1fs", (LONGPRESS_MS * (1.0f - p)) / 1000.0f);
            lv_label_set_text(_chord, buf);
        } else if (_hold_start) {
            // 中途松手：撤回，回到读命令的状态
            _hold_start = 0;
            set_glyph(false, 0.0f, 0);
            lv_obj_set_style_opa(_tool, LV_OPA_COVER, 0);
            lv_obj_set_style_opa(_hint, LV_OPA_COVER, 0);
            lv_obj_set_style_opa(_reason, LV_OPA_COVER, 0);
            ui::arc_set_color(_rim, CINNABAR);
            lv_label_set_text(_chord, "hold A · deny B");
        }
        return;
    }

    if (hal.btnA.wasClicked()) {
        if (ble::send_permission(s.ble.prompt_id, ble::Decision::Once)) apply_settled(true);
    }
}

void AppWard::onClose()
{
    _key.reset();

    LvglLockGuard lock;
    // 一个都不能漏。mooncake 会复用实例，残留指针会指向已销毁对象
    if (_stage) lv_obj_delete(_stage);
    _stage = _rim = _orbit = _tool = _hint = _reason = _needle = _chord = nullptr;
    _passkey = _ghost = _clip = _fill = nullptr;
    ui::root_release();
    ui::set_luma(ui::Luma::Normal);
}

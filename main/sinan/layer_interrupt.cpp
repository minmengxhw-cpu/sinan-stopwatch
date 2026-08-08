/*
 * layer_interrupt.cpp
 *
 * 审批交互继承自旧的「守」，是全项目戏剧浓度最高的一段，原样保留：
 * 危险命令长按 A 的 0.8 秒里，文字退场，团团自下而上被石绿填满，
 * 填满即放行。松手可撤。
 */
#include "layer_interrupt.h"
#include "bridge_ble.h"
#include "danger.h"
#include "design.h"
#include "gaze_fsm.h"
#include "haptics.h"
#include "precession.h"
#include "ring.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cstdio>

namespace sinan::layer_interrupt {

using namespace design;
namespace ui = sinan::ui;

namespace {

// 一个请求给 90 秒。到时不自动批准也不自动拒绝 —— 超时替用户做决定是不可接受的
constexpr uint32_t kPromptWindowMs = 90000;
constexpr uint32_t kDoneHoldMs  = 1200;
constexpr uint32_t kErrorHoldMs = 8000;

enum class Face { Pending, Settled, Notice, Pairing };

lv_obj_t* s_stage  = nullptr;
lv_obj_t* s_rim    = nullptr;
lv_obj_t* s_orbit  = nullptr;
lv_obj_t* s_tool   = nullptr;
lv_obj_t* s_hint   = nullptr;
lv_obj_t* s_reason = nullptr;
lv_obj_t* s_ghost  = nullptr;   // 团团点阵，常驻的幽灵层
lv_obj_t* s_clip   = nullptr;   // 裁剪容器，高度即长按进度
lv_obj_t* s_fill   = nullptr;   // 同一张图，按状态色填
lv_obj_t* s_chord  = nullptr;

Face s_face = Face::Notice;
bool s_grave = false;
std::string s_shown_id;
uint32_t s_settled_at = 0;
uint32_t s_hold_start = 0;
uint32_t s_notice_at  = 0;
bool s_done = false;            // 中断已了结，shell 可以收台

/* --------------------------- 点阵 --------------------------- */

void set_glyph(bool visible, float fill, uint32_t hue)
{
    static constexpr int GS = 264;
    if (!s_ghost || !s_clip) return;

    if (!visible) {
        lv_obj_add_flag(s_ghost, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_clip, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_ghost, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_clip, LV_OBJ_FLAG_HIDDEN);

    const int h = static_cast<int>(GS * std::clamp(fill, 0.0f, 1.0f));
    lv_obj_set_height(s_clip, h > 0 ? h : 1);
    lv_obj_align(s_clip, LV_ALIGN_CENTER, 0, (GS - h) / 2);
    lv_obj_set_style_image_recolor(s_fill, c(hue), 0);
}

/* --------------------------- 四张脸 --------------------------- */

void apply_pending(const Snapshot& s, uint32_t now)
{
    s_face = Face::Pending;
    s_shown_id = s.ble.prompt_id;
    s_done = false;
    ui::set_luma(ui::Luma::Alert);

    const Risk risk = assess(s.ble.prompt_tool, s.ble.prompt_hint);
    s_grave = (risk == Risk::Grave);
    const uint32_t hue = s_grave ? CINNABAR : (risk == Risk::Elevated ? AMBER : MALACHITE);

    gaze::notify(s_grave ? gaze::Mood::Guard : gaze::Mood::Alert,
                 s_grave ? "grave prompt" : "prompt", now);
    if (s_grave) haptics::play(haptics::Cue::Warn);

    ui::stop_breathe(s_rim);
    ui::arc_set_color(s_rim, hue);
    ui::arc_set_range(s_rim, 0, 359.9f);

    // Orbit 刻度按危险等级点亮的密度不同：越危险，环上越"实"
    const int lit = s_grave ? 36 : (risk == Risk::Elevated ? 18 : 9);
    ui::ticks_reset_color(s_orbit, LACQUER);
    for (int i = 0; i < 36; i += (36 / lit)) {
        // 底部 60° 留空给弦区那行提示，否则刻度会从文字里穿过去
        if (i >= 15 && i <= 21) continue;
        ui::tick_set_color(s_orbit, i, hue);
    }

    // 读命令的时候屏幕全让给文字，字形退场
    set_glyph(false, 0.0f, 0);
    lv_obj_set_style_text_font(s_tool, SN_FONT_MONO_L, 0);
    lv_label_set_text(s_tool, s.ble.prompt_tool.c_str());
    lv_obj_set_style_text_color(s_tool, c(hue), 0);
    lv_label_set_text(s_hint, s.ble.prompt_hint.c_str());

    const char* why = risk_reason(s.ble.prompt_tool, s.ble.prompt_hint);
    lv_label_set_text(s_reason, why);
    lv_obj_set_style_text_color(s_reason, c(hue), 0);

    // 危险操作明说需要长按。UI 不该让人猜为什么按了没反应
    lv_label_set_text(s_chord, s_grave ? "hold A \xc2\xb7 deny B" : "A allow \xc2\xb7 B deny");
    lv_obj_set_style_text_color(s_chord, c(s_grave ? CINNABAR : BRONZE), 0);
}

void apply_settled(bool approved, uint32_t now)
{
    s_face = Face::Settled;
    s_settled_at = now;
    s_shown_id.clear();

    ui::stop_breathe(s_rim);
    ui::bloom(s_rim, 0, approved ? MALACHITE : CINNABAR, T_STATE);
    ui::ticks_reset_color(s_orbit, approved ? MALACHITE : CINNABAR);

    lv_label_set_text(s_tool, approved ? "ALLOWED" : "DENIED");
    lv_obj_set_style_text_font(s_tool, SN_FONT_MONO_L, 0);
    lv_obj_set_style_text_color(s_tool, c(approved ? MALACHITE : CINNABAR), 0);
    lv_label_set_text(s_hint, "");
    lv_label_set_text(s_reason, "");
    lv_label_set_text(s_chord, "");
    s_hold_start = 0;

    haptics::play(approved ? haptics::Cue::Ok : haptics::Cue::Ng);
    shell::on_decision_made(approved, now);  // 回 Rest 时团团要记得这件事
}

void apply_notice(const IrqEvent& e, uint32_t now)
{
    s_face = Face::Notice;
    s_notice_at = now;
    s_done = false;
    ui::set_luma(ui::Luma::Alert);

    const bool err = (e.kind == IrqKind::Error);
    const uint32_t hue = err ? CINNABAR : MALACHITE;

    ui::stop_breathe(s_rim);
    ui::arc_set_color(s_rim, hue);
    ui::arc_set_range(s_rim, 0, 359.9f);
    ui::ticks_reset_color(s_orbit, hue);
    set_glyph(false, 0.0f, 0);

    lv_obj_set_style_text_font(s_tool, SN_FONT_MONO_L, 0);
    lv_label_set_text(s_tool, e.title.c_str());
    lv_obj_set_style_text_color(s_tool, c(hue), 0);
    lv_label_set_text(s_hint, e.body.c_str());
    lv_label_set_text(s_reason, "");
    lv_label_set_text(s_chord, err ? "B dismiss" : "");
    lv_obj_set_style_text_color(s_chord, c(BRONZE_D), 0);

    haptics::play(err ? haptics::Cue::Ng : haptics::Cue::Ok);
}

void apply_pairing(uint32_t code)
{
    s_face = Face::Pairing;
    s_done = false;
    ui::set_luma(ui::Luma::Alert);

    ui::stop_breathe(s_rim);
    ui::arc_set_color(s_rim, AMBER);
    ui::arc_set_range(s_rim, 0, 359.9f);
    ui::breathe(s_rim, T_BREATH, 90, 255);
    ui::ticks_reset_color(s_orbit, LACQUER);
    set_glyph(false, 0.0f, 0);

    char digits[8];
    std::snprintf(digits, sizeof(digits), "%06u", static_cast<unsigned>(code));
    lv_label_set_text(s_tool, digits);
    lv_obj_set_style_text_font(s_tool, SN_FONT_NUM_L, 0);
    lv_obj_set_style_text_color(s_tool, c(SILK), 0);
    lv_label_set_text(s_hint, "");
    lv_label_set_text(s_reason, "");
    lv_label_set_text(s_chord, "type this code on your Mac");
    lv_obj_set_style_text_color(s_chord, c(AMBER), 0);
}

/* --------------------------- 审批长按进度 --------------------------- */
/* wasClicked / pressedFor 由 shell 统一读取后经 key() 路由进来，
   这里只用 isPressed 这类不消费标志的接口画长按进度 */

void draw_hold_progress()
{
    auto& hal = GetHAL();
    if (hal.btnA.isPressed()) {
        if (s_hold_start == 0) s_hold_start = hal.millis();
        const float p = std::clamp(
            static_cast<float>(hal.millis() - s_hold_start) / LONGPRESS_MS, 0.0f, 1.0f);
        // 手指按下去，屏幕就从「读命令」变成「看确认」
        lv_obj_set_style_opa(s_tool, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(s_hint, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(s_reason, LV_OPA_TRANSP, 0);
        set_glyph(true, p, MALACHITE);
        ui::arc_set_color(s_rim, MALACHITE);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1fs", (LONGPRESS_MS * (1.0f - p)) / 1000.0f);
        lv_label_set_text(s_chord, buf);
    } else if (s_hold_start) {
        // 中途松手：撤回，回到读命令的状态
        s_hold_start = 0;
        set_glyph(false, 0.0f, 0);
        lv_obj_set_style_opa(s_tool, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(s_hint, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(s_reason, LV_OPA_COVER, 0);
        ui::arc_set_color(s_rim, CINNABAR);
        lv_label_set_text(s_chord, "hold A \xc2\xb7 deny B");
    }
}

void decide(bool approved)
{
    const auto s = State::get().snapshot();
    if (!s.ble.has_prompt) return;
    const auto d = approved ? ble::Decision::Once : ble::Decision::Deny;
    if (ble::send_permission(s.ble.prompt_id, d)) {
        apply_settled(approved, GetHAL().millis());
    }
}

}  // namespace

void create()
{
    LvglLockGuard lock;
    s_stage = ui::stage(ui::precession_root());

    // Rim：唯一一件两米外看得清的东西
    s_rim = ui::arc(s_stage, {R_RIM, W_RIM, INDIGO, LACQUER, true}, 0, 40);

    // Orbit：待决时显示危险等级的刻度
    s_orbit = ui::ticks(s_stage, R_ORBIT, 36, 14, 3, LACQUER);

    // Core：工具名（大字）+ 命令内容（等宽，可折行）
    s_tool = ui::text(s_stage, "", SN_FONT_MONO_L, SILK);
    lv_obj_align(s_tool, LV_ALIGN_CENTER, 0, -62);

    s_hint = ui::mono_block(s_stage, "", SN_FONT_MONO_S, SILK_D, 250);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 6);

    s_reason = ui::text(s_stage, "", SN_FONT_MONO_S, CINNABAR);
    lv_obj_align(s_reason, LV_ALIGN_CENTER, 0, 82);

    /* 团团点阵：两层同一张 RGBA PNG。底层是暗鎏金的幽灵，始终在；
       上层按状态染色，装在高度可变的裁剪容器里 —— 长按时容器从下往上长 */
    static constexpr int GS = 264;
    const char* path = "A:/spiflash/tuan/glyph.png";

    s_ghost = lv_image_create(s_stage);
    lv_image_set_src(s_ghost, path);
    lv_obj_center(s_ghost);
    lv_obj_set_style_image_recolor(s_ghost, c(BRONZE_D), 0);
    lv_obj_set_style_image_recolor_opa(s_ghost, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_ghost, 66, 0);
    lv_obj_add_flag(s_ghost, LV_OBJ_FLAG_HIDDEN);

    s_clip = lv_obj_create(s_stage);
    lv_obj_set_size(s_clip, GS, GS);
    lv_obj_center(s_clip);
    lv_obj_set_style_bg_opa(s_clip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_clip, 0, 0);
    lv_obj_set_style_pad_all(s_clip, 0, 0);
    lv_obj_set_style_clip_corner(s_clip, false, 0);
    lv_obj_remove_flag(s_clip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_clip, LV_OBJ_FLAG_HIDDEN);

    s_fill = lv_image_create(s_clip);
    lv_image_set_src(s_fill, path);
    lv_image_set_inner_align(s_fill, LV_IMAGE_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_image_recolor_opa(s_fill, LV_OPA_COVER, 0);
    lv_obj_set_size(s_fill, GS, GS);
    lv_obj_align(s_fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    s_chord = ui::chord(s_stage, "");

    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    mclog::tagInfo("Interrupt", "created");
}

void enter()
{
    LvglLockGuard lock;
    s_done = false;
    s_shown_id.clear();
    s_hold_start = 0;
    lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    ui::set_luma(ui::Luma::Alert);
}

void exit()
{
    LvglLockGuard lock;
    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    ui::set_luma(ui::Luma::Quiet);
}

bool tick(uint32_t now)
{
    const auto s = State::get().snapshot();

    LvglLockGuard lock;

    /* ---- 落定：只停留一瞬 ---- */
    if (s_face == Face::Settled) {
        if (now - s_settled_at > T_STATE + 700) s_done = true;
        return s_done;
    }

    /* ---- 配对码：码消失（配上或超时）就退 ---- */
    if (s.ble.passkey_pending && !s.ble.has_prompt) {
        if (s_face != Face::Pairing) apply_pairing(s.ble.passkey);
        return false;
    }
    if (s_face == Face::Pairing) {
        s_done = true;   // passkey_pending 已消失
        return true;
    }

    /* ---- 审批 ---- */
    if (s.ble.has_prompt) {
        if (s_face != Face::Pending || s_shown_id != s.ble.prompt_id) {
            apply_pending(s, now);
        } else {
            // Rim 按剩余时间收缩。时间本身就是弧长
            const uint32_t elapsed = now - s.ble.prompt_since;
            const float left = elapsed >= kPromptWindowMs
                                   ? 0.0f
                                   : 1.0f - static_cast<float>(elapsed) / kPromptWindowMs;
            ui::arc_set_progress(s_rim, left);
            if (s_grave) draw_hold_progress();
        }
        return false;
    }
    if (s_face == Face::Pending) {
        // prompt 消失了：可能在别处批了，也可能超时。不声张地收台
        s_done = true;
        return true;
    }

    /* ---- 通知 ---- */
    if (s.irq.active) {
        const bool err = (s.irq.kind == IrqKind::Error);
        if (s_face != Face::Notice) apply_notice(s.irq, now);
        if (!err && now - s_notice_at > kDoneHoldMs) {
            State::get().clearIrq();
            s_done = true;
            return true;
        }
        if (err && now - s_notice_at > kErrorHoldMs) {
            State::get().clearIrq();
            s_done = true;
            return true;
        }
        return false;
    }
    if (s_face == Face::Notice) {
        s_done = true;
        return true;
    }

    return s_done;
}

bool key(shell::Key k)
{
    if (s_face == Face::Pending) {
        // 危险操作只认长按。这 0.8 秒是唯一挡在不可逆操作前面的东西
        if (k == shell::Key::BShort) { decide(false); return true; }
        if (!s_grave && k == shell::Key::AShort) { decide(true); return true; }
        if (s_grave && k == shell::Key::ALong)   { decide(true); return true; }
        return true;  // Pending 期间吞掉所有按键，不许漏到下层
    }
    if (k == shell::Key::BShort && s_face == Face::Notice) {
        State::get().clearIrq();
        s_done = true;
        return true;
    }
    return s_face == Face::Pairing;  // 配对时也不许按键穿透
}

bool showing_grave()
{
    return s_face == Face::Pending && s_grave;
}

}  // namespace sinan::layer_interrupt

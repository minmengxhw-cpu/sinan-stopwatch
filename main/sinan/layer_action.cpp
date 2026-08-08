/*
 * layer_action.cpp
 *
 * 布局（12 点为 0°，顺时针）：
 *   0° FAST | 60° NG | 120° OK | 180° PLAN | 240° AI | 中央对讲
 *   （300° 留空 —— 六等分的第六格留给弦区开口，刻度不穿字）
 *
 * 触发反馈 = 弧段点亮语义色 320ms 后熄灭，没有滑入滑出。
 * HID 未连接时弦区一行小字，不弹窗。
 */
#include "layer_action.h"
#include "bridge_hid.h"
#include "design.h"
#include "haptics.h"
#include "precession.h"
#include "ring.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>

namespace sinan::layer_action {

using namespace design;
namespace ui = sinan::ui;
using mooncake::mclog;

namespace {

constexpr int kSegs = 5;
constexpr float kSegSpan = 34.0f;             // 段弧长
constexpr float kSegDeg[kSegs] = {0, 60, 120, 180, 240};
constexpr const char* kSegName[kSegs] = {"FAST", "NG", "OK", "PLAN", "AI"};
constexpr hid::Action kSegAction[kSegs] = {
    hid::Action::Fast, hid::Action::Ng, hid::Action::Ok,
    hid::Action::Plan, hid::Action::Ai,
};

lv_obj_t* s_stage = nullptr;
lv_obj_t* s_rim   = nullptr;
lv_obj_t* s_seg[kSegs] = {};
lv_obj_t* s_seg_label[kSegs] = {};
lv_obj_t* s_guide_ok = nullptr;   // 石绿导轨，A 侧
lv_obj_t* s_guide_ng = nullptr;   // 朱砂导轨，B 侧
lv_obj_t* s_ear   = nullptr;      // 中央对讲：一只闭着的耳朵
lv_obj_t* s_chord = nullptr;

uint32_t s_flash_at = 0;
int s_flash_seg = -1;

void flash(int i, uint32_t hue)
{
    ui::arc_set_color(s_seg[i], hue);
    s_flash_seg = i;
    s_flash_at = GetHAL().millis();
}

void trigger(int i)
{
    const hid::Action a = kSegAction[i];
    const char* name = kSegName[i];

    if (!hid::connected()) {
        haptics::vibe_only(haptics::Cue::Soft);
        lv_label_set_text(s_chord, "no hid · pair in bluetooth settings");
        lv_obj_set_style_text_color(s_chord, c(INDIGO), 0);
        return;
    }

    if (!hid::send(a)) {
        lv_label_set_text(s_chord, "hid busy");
        lv_obj_set_style_text_color(s_chord, c(INDIGO), 0);
        return;
    }

    // 颜色语义全局固定：NG 朱砂，OK 石绿，其余鎏金
    uint32_t hue = BRONZE;
    if (a == hid::Action::Ng)     { hue = CINNABAR;  haptics::play(haptics::Cue::Ng); }
    else if (a == hid::Action::Ok){ hue = MALACHITE; haptics::play(haptics::Cue::Ok); }
    else                          { haptics::play(haptics::Cue::Tick); }
    flash(i, hue);

    char line[32];
    std::snprintf(line, sizeof(line), "%s → hid", name);
    lv_label_set_text(s_chord, line);
    lv_obj_set_style_text_color(s_chord, c(hue), 0);
}

}  // namespace

void create()
{
    LvglLockGuard lock;
    s_stage = ui::stage(ui::precession_root());

    // Rim 整圈细弧：鎏金底
    s_rim = ui::arc(s_stage, {R_RIM, 4, BRONZE, INK, true}, 0, 359.9f);

    // 配色导轨：从屏缘伸向 OK / NG 段的短弧，把物理 A/B 键和屏上动作连起来
    // OK 在 120°（右下，靠 A 键一侧），NG 在 60°（左下，靠 B 键一侧）
    s_guide_ok = ui::arc(s_stage, {R_RIM, W_RIM, MALACHITE, INK, true}, 108, 132);
    s_guide_ng = ui::arc(s_stage, {R_RIM, W_RIM, CINNABAR, INK, true}, 48, 72);

    for (int i = 0; i < kSegs; i++) {
        const float mid = kSegDeg[i];
        s_seg[i] = ui::arc(s_stage, {R_ORBIT, W_ORBIT + 4, LACQUER, INK, true},
                           mid - kSegSpan / 2, mid + kSegSpan / 2);
        s_seg_label[i] = ui::text(s_stage, kSegName[i], SN_FONT_MONO_S, BRONZE);
        ui::place_polar(s_seg_label[i], R_ORBIT_IN - 26, mid);
    }

    // 中央对讲：一只闭着的耳朵。长按中心它才睁开
    s_ear = ui::arc(s_stage, {58, 3, BRONZE_D, INK, true}, 0, 359.9f);
    lv_obj_t* hint = ui::text(s_stage, "TALK", SN_FONT_MONO_S, BRONZE_D);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 84);

    s_chord = ui::chord(s_stage, "A ok · B ng · hold center to talk");

    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    mclog::tagInfo("Action", "created");
}

void enter()
{
    LvglLockGuard lock;
    lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    ui::set_luma(ui::Luma::Normal);
    lv_label_set_text(s_chord, hid::connected() ? "A ok · B ng · hold center to talk"
                                                : "no hid · pair in bluetooth settings");
    lv_obj_set_style_text_color(s_chord, c(hid::connected() ? BRONZE : INDIGO), 0);
}

void exit()
{
    LvglLockGuard lock;
    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
}

void tick(uint32_t now)
{
    // 段弧点亮 320ms 后回到漆色
    if (s_flash_seg >= 0 && now - s_flash_at > T_FAST) {
        LvglLockGuard lock;
        ui::arc_set_color(s_seg[s_flash_seg], LACQUER);
        s_flash_seg = -1;
    }
}

bool key(shell::Key k)
{
    LvglLockGuard lock;
    switch (k) {
        case shell::Key::AShort: trigger(2); return true;  // OK
        case shell::Key::BShort: trigger(1); return true;  // NG
        default: return false;
    }
}

void tap_polar(float deg, float r, uint32_t)
{
    // 命中判定：Orbit 带 ±34px 内，找角度最近的段
    if (r < R_ORBIT - 34 || r > R_ORBIT + 34) return;
    int best = -1;
    float best_d = kSegSpan / 2 + 6.0f;  // 段间隙不命中
    for (int i = 0; i < kSegs; i++) {
        float d = deg - kSegDeg[i];
        while (d > 180.0f) d -= 360.0f;
        while (d < -180.0f) d += 360.0f;
        if (d < 0) d = -d;
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best >= 0) {
        LvglLockGuard lock;
        trigger(best);
    }
}

}  // namespace sinan::layer_action

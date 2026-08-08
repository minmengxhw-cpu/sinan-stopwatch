/*
 * talk_overlay.cpp
 *
 * 电平环用的是 HAL 白送的 20 段频谱，沿圆周每段 18°。
 * 按住说话比"点一下开始再点一下结束"少一次误操作。
 */
#include "talk_overlay.h"
#include "design.h"
#include "haptics.h"
#include "precession.h"
#include "ring.h"
#include "state.h"
#include "voice.h"
#include <hal/hal.h>
#include <algorithm>
#include <cmath>

namespace sinan::talk {

using namespace design;
namespace ui = sinan::ui;

namespace {

constexpr uint32_t kMaxRecMs = 12000;  // 说太久转写会慢到不像话
constexpr uint32_t kReplyHoldMs = 3000;
constexpr int kBands = 20;
constexpr int kBarMin = 6;
constexpr int kBarMax = 46;

lv_obj_t* s_stage = nullptr;
lv_obj_t* s_rim   = nullptr;
lv_obj_t* s_bars[kBands] = {};
lv_obj_t* s_text  = nullptr;
lv_obj_t* s_chord = nullptr;

bool s_active = false;
uint32_t s_rec_start = 0;
uint32_t s_reply_at = 0;
float s_spin = 0.0f;
int s_phase = 0;  // 1 录音 / 2 等待 / 3 读结果

void draw_levels()
{
    auto& hal = GetHAL();
    hal.updateAudioSpectrum();
    const auto& f = hal.getAudioSpectrum();

    for (int i = 0; i < kBands; i++) {
        const float v = std::clamp(f.bands[i], 0.0f, 1.0f);
        const int h = kBarMin + static_cast<int>((kBarMax - kBarMin) * v);
        lv_obj_set_height(s_bars[i], h);
        lv_obj_set_style_transform_pivot_y(s_bars[i], h / 2, 0);
        // 从暗鎏金推向石绿，越响越亮。颜色只有一条轴，不搞彩虹
        lv_obj_set_style_bg_color(s_bars[i], c(v > 0.55f ? MALACHITE : BRONZE), 0);
        ui::place_polar(s_bars[i], R_ORBIT, 360.0f * i / kBands);
    }
}

}  // namespace

void create()
{
    LvglLockGuard lock;
    s_stage = ui::stage(ui::precession_root());
    // 覆盖层不能遮挡下面的层：底板透明，只画自己的东西
    lv_obj_set_style_bg_opa(s_stage, LV_OPA_80, 0);

    s_rim = ui::arc(s_stage, {R_RIM, 4, MALACHITE, INK, true}, 0, 359.9f);

    for (int i = 0; i < kBands; i++) {
        lv_obj_t* b = lv_obj_create(s_stage);
        lv_obj_set_size(b, 5, kBarMin);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_set_style_bg_color(b, c(BRONZE_D), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        const float deg = 360.0f * i / kBands;
        lv_obj_set_style_transform_rotation(b, static_cast<int32_t>(deg * 10), 0);
        lv_obj_set_style_transform_pivot_x(b, 2, 0);
        lv_obj_set_style_transform_pivot_y(b, kBarMin / 2, 0);
        ui::place_polar(b, R_ORBIT, deg);
        s_bars[i] = b;
    }

    s_text = ui::mono_block(s_stage, "", SN_FONT_MONO_S, SILK, 250);
    lv_obj_align(s_text, LV_ALIGN_CENTER, 0, 0);
    s_chord = ui::chord(s_stage, "listening");

    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
}

void begin(uint32_t now)
{
    if (s_active) return;
    s_active = true;
    s_phase = 1;
    s_rec_start = now;
    s_spin = 0.0f;

    voice::begin();
    haptics::vibe_only(haptics::Cue::Soft);  // 录音期间不播音，免得进麦

    LvglLockGuard lock;
    lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    ui::set_luma(ui::Luma::Alert);
    ui::stop_breathe(s_rim);
    ui::arc_set_color(s_rim, MALACHITE);
    lv_label_set_text(s_text, "");
    lv_label_set_text(s_chord, "listening");
}

void end()
{
    if (!s_active || s_phase != 1) return;
    s_phase = 2;
    voice::end();
    LvglLockGuard lock;
    lv_label_set_text(s_chord, "thinking");
    ui::arc_set_color(s_rim, AMBER);
}

void abort()
{
    if (!s_active) return;
    voice::abort();
    s_active = false;
    s_phase = 0;
    LvglLockGuard lock;
    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
}

bool active() { return s_active; }

void tick(uint32_t now)
{
    if (!s_active) return;

    LvglLockGuard lock;
    const auto s = State::get().snapshot();

    if (s_phase == 1) {
        draw_levels();
        const uint32_t used = now - s_rec_start;
        ui::arc_set_progress(s_rim, 1.0f - std::min(1.0f, static_cast<float>(used) / kMaxRecMs));
        if (used > kMaxRecMs) end();
        return;
    }

    if (s_phase == 2) {
        // 等待期间 Rim 跑一段短弧转圈。转圈就够了，不需要文字说"加载中"
        s_spin += 3.0f;
        if (s_spin >= 360.0f) s_spin -= 360.0f;
        ui::arc_set_range(s_rim, s_spin, s_spin + 50.0f);

        if (!s.voice.heard.empty()) lv_label_set_text(s_text, s.voice.heard.c_str());
        if (s.voice.phase == VoicePhase::Speaking ||
            (s.voice.phase == VoicePhase::Idle && !s.voice.reply.empty())) {
            s_phase = 3;
            s_reply_at = 0;
            ui::arc_set_color(s_rim, SILK);
            ui::arc_set_range(s_rim, 0, 359.9f);
            lv_label_set_text(s_chord, "");
        }
        return;
    }

    if (s_phase == 3) {
        lv_label_set_text(s_text, s.voice.reply.empty() ? s.voice.heard.c_str()
                                                        : s.voice.reply.c_str());
        if (s.voice.phase == VoicePhase::Idle) {
            if (s_reply_at == 0) s_reply_at = now;
            if (now - s_reply_at > kReplyHoldMs) {
                s_active = false;
                s_phase = 0;
                lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

}  // namespace sinan::talk

/*
 * layer_rest.cpp — 寐。
 *
 * 分层刷新率是这个页面能同时"好看"和"跑得动"的全部原因：
 * 照片层 8 FPS（每帧挪 1–2px，要的就是慢），其余按事件驱动。
 * 不要为了"更流畅"把照片层提速 —— 424KB 一帧，总线会被打满。
 *
 * 与旧 app_gaze 的差异（按重构规格 §9）：
 *   - 外圈 outrun 秒点删除，轨道让给项圈珠串
 *   - 三版式轮询删除：默认只有寐；敲击/点中心露时 6s；大字模式进 Settings
 *   - 李萨如漂移保留，但它是 anti-burn-in，不再被叙事成「生命」
 *   - 晕影呼吸幅度由 gaze FSM 的 mood 决定，动效必须答得出「因为什么」
 */
#include "layer_rest.h"
#include "beads.h"
#include "design.h"
#include "gaze_fsm.h"
#include "haptics.h"
#include "input_map.h"
#include "photo_store.h"
#include "precession.h"
#include "ring.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sinan::layer_rest {

using namespace design;
namespace ui = sinan::ui;
namespace photo = sinan::photo;

namespace {

constexpr uint32_t kPhotoPeriod = 125;   // 8 FPS
constexpr uint32_t kSwapAfter   = 5 * 60 * 1000;
constexpr uint32_t kWakeHold    = 6000;
constexpr uint32_t kFadeMs      = 900;
constexpr uint32_t kDreamStepMs = 60000;
constexpr uint32_t kBeadRebuildMs = 1000;

// 李萨如漂移：anti-burn-in。两个周期互质，轨迹准周期，像素从不静止
constexpr float kDriftAmp  = 22.0f;
constexpr float kDriftSecX = 14 * 60.0f;
constexpr float kDriftSecY = 19 * 60.0f;

// 成年犬静息呼吸每分钟 15–30 次，取 18。幅度由 mood 决定
constexpr float kBreathBpm = 18.0f;

// 视差幅度，别调大，大了就俗了
constexpr float kTiltPhoto = 5.0f;
constexpr float kTiltText  = 2.0f;

constexpr float kPi = 3.14159265358979f;
constexpr int kVignetteRings = 24;

lv_obj_t* s_stage = nullptr;
lv_obj_t* s_photo = nullptr;
lv_obj_t* s_photo_next = nullptr;
lv_obj_t* s_hint  = nullptr;
lv_obj_t* s_vig[kVignetteRings] = {};
lv_obj_t* s_rim   = nullptr;   // 分钟弧：一圈走满即一小时
lv_obj_t* s_scrim = nullptr;
lv_obj_t* s_time  = nullptr;
lv_obj_t* s_date  = nullptr;
lv_obj_t* s_ghost = nullptr;   // 灵魂点阵
lv_obj_t* s_chord = nullptr;   // 焦点珠的 label

const lv_image_dsc_t* s_cur_dsc  = nullptr;
const lv_image_dsc_t* s_next_dsc = nullptr;

int s_index = 0;
bool s_locked = false;
int  s_last_min = -1;
bool s_visible = false;

uint32_t s_t_photo = 0;
uint32_t s_t_swap  = 0;
uint32_t s_t_woke  = 0;
uint32_t s_t_dream = 0;
uint32_t s_t_beads = 0;

float s_tilt_x = 0.0f;
float s_tilt_y = 0.0f;

// mood 表现缓存：只在变化时改样式，避免每帧刷 LVGL
int      s_applied_breath = -1;
uint8_t  s_applied_opa    = 0xFF;
uint32_t s_applied_hue    = 0;
int      s_applied_bead   = -1;

int vig_radius(int i, int shift) { return R_SAFE - 18 + i * 2 + shift; }

/* --------------------------- 构建 --------------------------- */

// 晕影用同心弧堆出来：不占 flash、半径可运行时改（呼吸靠这个）、
// 而且它天然属于"一切都是弧"的设计语言
void build_vignette()
{
    for (int i = 0; i < kVignetteRings; i++) {
        const float t = static_cast<float>(i) / (kVignetteRings - 1);
        const int opa = static_cast<int>(255.0f * t * t);  // 平方曲线，线性会看出台阶
        s_vig[i] = ui::arc(s_stage, {vig_radius(i, 0), 3, INK, INK, false}, 0, 359.9f);
        lv_obj_set_style_arc_opa(s_vig[i], opa, LV_PART_INDICATOR);
    }
}

void on_swap_done(lv_timer_t* tm);

void swap_photo()
{
    if (photo::count() < 2 || s_next_dsc) return;  // 上一次淡入还没完就别叠

    const int next = (s_index + 1) % photo::count();
    const lv_image_dsc_t* dsc = photo::acquire(next);
    if (!dsc) return;

    // 交叉淡入：新图在上层从透明推到不透明，完事再交换指针
    lv_image_set_src(s_photo_next, dsc);
    lv_image_set_offset_x(s_photo_next, -photo::PAN);
    lv_image_set_offset_y(s_photo_next, -photo::PAN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_photo_next);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(o), v, 0);
    });
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, kFadeMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    s_next_dsc = s_cur_dsc;   // 旧图留到淡入结束再释放
    s_cur_dsc  = dsc;
    s_index    = next;
    s_t_swap   = GetHAL().millis();

    lv_timer_t* tm = lv_timer_create(on_swap_done, kFadeMs + 50, nullptr);
    lv_timer_set_repeat_count(tm, 1);
}

void on_swap_done(lv_timer_t* tm)
{
    lv_timer_delete(tm);
    if (!s_photo) return;
    lv_image_set_src(s_photo, s_cur_dsc);
    lv_obj_set_style_opa(s_photo_next, LV_OPA_TRANSP, 0);
    if (s_next_dsc) {
        photo::release(s_next_dsc);
        s_next_dsc = nullptr;
    }
}

/* --------------------------- 逐帧 --------------------------- */

void tick_photo(uint32_t now)
{
    if (now - s_t_photo < kPhotoPeriod) return;
    s_t_photo = now;

    const float sec = now / 1000.0f;

    if (s_cur_dsc) {
        // 漂移只改 offset，纯 blit，不做任何重采样
        const float dx = kDriftAmp * std::sin(2 * kPi * sec / kDriftSecX);
        const float dy = kDriftAmp * std::sin(2 * kPi * sec / kDriftSecY);
        const int ox = static_cast<int>(photo::PAN + dx - s_tilt_x * kTiltPhoto);
        const int oy = static_cast<int>(photo::PAN + dy - s_tilt_y * kTiltPhoto);
        lv_image_set_offset_x(s_photo, -std::clamp(ox, 0, photo::PAN * 2));
        lv_image_set_offset_y(s_photo, -std::clamp(oy, 0, photo::PAN * 2));
    }

    // 呼吸：只动晕影半径，幅度由 mood 给
    const int amp = gaze::breath_px();
    const float br = amp > 0 ? std::sin(2 * kPi * sec * kBreathBpm / 60.0f) : 0.0f;
    const int shift = static_cast<int>(br * amp);
    if (amp != s_applied_breath || shift != 0) {
        for (int i = 0; i < kVignetteRings; i++) {
            const int r = vig_radius(i, shift);
            lv_obj_set_size(s_vig[i], r * 2 + 3, r * 2 + 3);
            lv_obj_center(s_vig[i]);
        }
        s_applied_breath = amp;
    }

    // 时间层朝倾斜的同方向轻移，与照片反向，视差才成立
    const int tx = static_cast<int>(s_tilt_x * kTiltText);
    lv_obj_align(s_time, LV_ALIGN_CENTER, tx, 118);
}

void tick_clock(bool force)
{
    const auto t = GetHAL().getTimeHms();
    if (!force && t.minute == s_last_min) return;
    s_last_min = t.minute;

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
    lv_label_set_text(s_time, buf);

    const auto d = GetHAL().getDateYmd();
    char db[16];
    std::snprintf(db, sizeof(db), "%02d.%02d", d.month, d.day);
    lv_label_set_text(s_date, db);
}

void tick_rim()
{
    const auto t = GetHAL().getTimeHms();
    ui::arc_set_progress(s_rim, (t.minute * 60.0f + t.second) / 3600.0f);
}

// 灵魂点阵的颜色与不透明度完全由 mood 翻译
void apply_mood()
{
    const uint8_t opa = gaze::ghost_opa();
    const uint32_t hue = gaze::ghost_hue();
    if (opa == s_applied_opa && hue == s_applied_hue) return;
    s_applied_opa = opa;
    s_applied_hue = hue;

    lv_obj_set_style_opa(s_ghost, opa, 0);
    lv_obj_set_style_image_recolor(s_ghost, c(hue), 0);
    lv_obj_set_style_image_recolor_opa(s_ghost, LV_OPA_COVER, 0);
}

void apply_bead_label()
{
    if (beads::focus() == s_applied_bead) return;
    s_applied_bead = beads::focus();

    const Bead& b = beads::get(beads::focus());
    if (b.kind == BeadKind::Empty) {
        lv_label_set_text(s_chord, "");
        return;
    }
    char line[56];
    std::snprintf(line, sizeof(line), "%s  %s", b.label, b.detail);
    lv_label_set_text(s_chord, line);
    lv_obj_set_style_text_color(s_chord, c(b.color), 0);
}

void wake_peek(uint32_t now, const char* reason)
{
    s_t_woke = now;
    gaze::poke(now);
    gaze::notify(gaze::Mood::Peek, reason, now);
    ui::set_luma(ui::Luma::Normal);
    lv_obj_set_style_opa(s_date, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_time, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_opa(s_scrim, LV_OPA_90, 0);
}

}  // namespace

void create()
{
    LvglLockGuard lock;
    s_stage = ui::stage(ui::precession_root());

    /* ---- 层 0：照片（身体） ---- */
    s_photo = lv_image_create(s_stage);
    lv_obj_set_size(s_photo, SCREEN, SCREEN);
    lv_obj_center(s_photo);
    lv_image_set_inner_align(s_photo, LV_IMAGE_ALIGN_TOP_LEFT);

    s_photo_next = lv_image_create(s_stage);
    lv_obj_set_size(s_photo_next, SCREEN, SCREEN);
    lv_obj_center(s_photo_next);
    lv_image_set_inner_align(s_photo_next, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_style_opa(s_photo_next, LV_OPA_TRANSP, 0);

    s_cur_dsc = photo::acquire(s_index);
    if (s_cur_dsc) {
        lv_image_set_src(s_photo, s_cur_dsc);
    } else {
        // 没有照片不是错误状态，是还没放照片。给一句能照做的话
        lv_obj_add_flag(s_photo, LV_OBJ_FLAG_HIDDEN);
        s_hint = ui::mono_block(s_stage, "drop a folder of photos\non Hardware Buddy",
                                SN_FONT_MONO_S, BRONZE_D, 300);
        lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 150);
    }

    /* ---- 层 1：晕影 ---- */
    build_vignette();

    /* ---- 层 1.5：Rim 分钟弧 ---- */
    s_rim = ui::arc(s_stage, {R_RIM, 4, BRONZE, INK, true}, 0, 1);

    /* ---- 层 2：灵魂点阵 ----
       与审批页同一张 glyph。静默时 26% 鎏金，状态来了才染色亮起 */
    s_ghost = lv_image_create(s_stage);
    lv_image_set_src(s_ghost, "A:/spiflash/tuan/glyph.png");
    lv_obj_center(s_ghost);
    lv_obj_set_style_image_recolor(s_ghost, c(BRONZE_D), 0);
    lv_obj_set_style_image_recolor_opa(s_ghost, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_ghost, 66, 0);

    /* ---- 层 3：时间衬底（Peek 才用） ---- */
    s_scrim = lv_obj_create(s_stage);
    lv_obj_set_size(s_scrim, SCREEN, 190);
    lv_obj_align(s_scrim, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(s_scrim, 0, 0);
    lv_obj_set_style_radius(s_scrim, 0, 0);
    lv_obj_set_style_pad_all(s_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_scrim, c(INK), 0);
    lv_obj_set_style_bg_grad_color(s_scrim, c(INK), 0);
    lv_obj_set_style_bg_grad_dir(s_scrim, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(s_scrim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(s_scrim, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);

    s_time = ui::text(s_stage, "--:--", SN_FONT_NUM_L, SILK);
    lv_obj_align(s_time, LV_ALIGN_CENTER, 0, 118);
    lv_obj_set_style_opa(s_time, LV_OPA_TRANSP, 0);

    s_date = ui::text(s_stage, "", SN_FONT_MONO_S, BRONZE);
    lv_obj_align(s_date, LV_ALIGN_CENTER, 0, 168);
    lv_obj_set_style_opa(s_date, LV_OPA_TRANSP, 0);

    /* ---- 层 4：项圈珠串 ---- */
    beads::mount(s_stage);
    s_chord = ui::chord(s_stage, "");

    tick_clock(true);
    mclog::tagInfo("Rest", "created, {} photos", photo::count());
}

void enter()
{
    LvglLockGuard lock;
    s_visible = true;
    lv_obj_remove_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
    ui::set_luma(ui::Luma::Quiet);
}

void exit()
{
    LvglLockGuard lock;
    s_visible = false;
    lv_obj_add_flag(s_stage, LV_OBJ_FLAG_HIDDEN);
}

void tick(uint32_t now)
{
    auto& hal = GetHAL();

    hal.updateImuData();
    const auto& imu = hal.getImuData();
    // 低通，否则视差会抖
    s_tilt_x = s_tilt_x * 0.9f + imu.accelX * 0.1f;
    s_tilt_y = s_tilt_y * 0.9f + imu.accelY * 0.1f;

    LvglLockGuard lock;

    // Peek 收时
    if (s_t_woke && now - s_t_woke > kWakeHold) {
        s_t_woke = 0;
        ui::set_luma(ui::Luma::Quiet);
        lv_obj_set_style_opa(s_date, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(s_time, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_grad_opa(s_scrim, LV_OPA_TRANSP, 0);
        if (gaze::mood() == gaze::Mood::Peek) {
            gaze::notify(gaze::Mood::Sleep, "peek timeout", now);
        }
    }

    // 珠串数据周期重建（重建规则在 beads.cpp，是确定性的）
    if (now - s_t_beads > kBeadRebuildMs) {
        s_t_beads = now;
        beads::rebuild_from(State::get().snapshot());
    }

    // Dream：珠串极慢自转，每 60s 一格。这是梦，不是动画
    if (gaze::beads_dream()) {
        if (now - s_t_dream > kDreamStepMs) {
            s_t_dream = now;
            beads::dream_step();
        }
    } else {
        s_t_dream = now;
    }

    apply_mood();
    apply_bead_label();
    tick_photo(now);
    tick_clock(false);
    tick_rim();

    if (!s_locked && now - s_t_swap > kSwapAfter) swap_photo();
}

bool key(shell::Key k)
{
    switch (k) {
        case shell::Key::ALong:
            // 锁定当前照片：唯独保留的深度操作
            s_locked = !s_locked;
            haptics::vibe_only(s_locked ? haptics::Cue::Warn : haptics::Cue::Soft);
            return true;
        case shell::Key::AShort:
            // 寐不打扰：轻振确认「按到了」，但什么都不发生
            haptics::vibe_only(haptics::Cue::Soft);
            return true;
        default:
            return false;
    }
}

void rim_slide(float delta_deg, bool fast, uint32_t now)
{
    gaze::poke(now);
    if (fast) {
        // 盘珠：累计角位移由 shell 折算成格，这里只负责拨
        beads::rotate(delta_deg > 0 ? 1 : -1);
        s_applied_bead = -1;  // 强制刷新弦区
        haptics::play(haptics::Cue::Tick);
    } else {
        // 慢滑是抚触，不是拨珠
        gaze::notify(gaze::Mood::Pet, "rim stroke", now);
        haptics::play(haptics::Cue::Pet);
    }
}

void tap_center(uint32_t now)
{
    wake_peek(now, "tap center");
}

void imu_tap(uint32_t now)
{
    wake_peek(now, "knock");
}

void next_photo()
{
    LvglLockGuard lock;
    swap_photo();
    haptics::play(haptics::Cue::Tick);
}

}  // namespace sinan::layer_rest

/*
 * app_gaze.cpp — 望。
 *
 * 分层刷新率是这个页面能同时"好看"和"跑得动"的全部原因：
 * 照片层 8 FPS（每帧挪 1–2px，反正要的就是慢），外圈点 30 FPS（只脏 24×24）。
 * 不要为了"更流畅"把照片层提到 30 FPS —— 424KB 一帧，总线会被打满。
 */
#include "app_gaze.h"
#include <sinan/design.h>
#include <sinan/ring.h>
#include <sinan/precession.h>
#include <sinan/photo_store.h>
#include <sinan/state.h>
#include <sinan/route.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace mooncake;
using namespace sinan;
using namespace sinan::design;
namespace ui = sinan::ui;
namespace photo = sinan::photo;

namespace {

constexpr uint32_t kPhotoPeriod = 125;   // 8 FPS
constexpr uint32_t kDotPeriod   = 33;    // 30 FPS
constexpr uint32_t kSwapAfter   = 5 * 60 * 1000;
constexpr uint32_t kWakeHold    = 6000;
constexpr uint32_t kFadeMs      = 900;
constexpr int kSwipeMin         = 60;

// 李萨如漂移。两个周期互质，轨迹准周期，肉眼看不出重复，
// 同时保证照片像素从不静止 —— 防烧屏是白送的
// 536px 资产在 466px 圆屏上每边只有 35px 安全区。22px 漂移再叠加
// IMU 视差会让偏构图照片越过安全中心；8px 仍能防静止烧屏，又不破坏人工构图。
constexpr float kDriftAmp  = 8.0f;
constexpr float kDriftSecX = 14 * 60.0f;
constexpr float kDriftSecY = 19 * 60.0f;

// 成年犬静息呼吸每分钟 15–30 次，取 18。你不会意识到它在呼吸，
// 但你会觉得它是活的
constexpr float kBreathBpm = 18.0f;
constexpr float kBreathPx  = 6.0f;

// 视差幅度，别调大，大了就俗了
constexpr float kTiltPhoto = 2.0f;
constexpr float kTiltText  = 2.0f;

constexpr float kPi = 3.14159265358979f;

/*
 * 晕影的第 i 圈半径。inner 由照片形态决定 ——
 * 早期版本只有一套固定半径，portrait 的照片放上去背景吃不干净，
 * 而我给你看的预览用的是分档的值，代码和预览对不上。
 */
int vig_radius(int i, int shift, int inner)
{
    const float step = static_cast<float>(VIG_OUTER - inner) / (VIG_RINGS - 1);
    return inner + static_cast<int>(i * step) + shift;
}

// 时间只在「时」版式常显，寐是敲桌临时露出，忆不显示。所以只有一个位置
constexpr int kTimeY = 118;

}  // namespace

AppGaze::AppGaze()
{
    static uint32_t launcher_color = BRONZE;
    setAppInfo().name = "Photos";
    setAppInfo().icon = (void*)&icon_badge;
    setAppInfo().userData = &launcher_color;
}

void AppGaze::onCreate()
{
    photo::init();
    mclog::tagInfo(getAppInfo().name, "on create, {} photos", photo::count());
}

void AppGaze::onOpen()
{
    _key = std::make_unique<input::KeyManager>();
    _last_min = -1;
    _t_swap = GetHAL().millis();

    LvglLockGuard lock;
    ui::root_acquire();   // 与 onClose 的 root_release 成对，漏了会全屏黑

    _stage = ui::stage(ui::precession_root());

    /* ---- 层 0：照片 ---- */
    _photo = lv_image_create(_stage);
    lv_obj_set_size(_photo, SCREEN, SCREEN);
    lv_obj_center(_photo);
    lv_image_set_inner_align(_photo, LV_IMAGE_ALIGN_TOP_LEFT);

    _photo_next = lv_image_create(_stage);
    lv_obj_set_size(_photo_next, SCREEN, SCREEN);
    lv_obj_center(_photo_next);
    lv_image_set_inner_align(_photo_next, LV_IMAGE_ALIGN_TOP_LEFT);
    lv_obj_set_style_opa(_photo_next, LV_OPA_TRANSP, 0);

    _vig_inner = photo::is_disc(_index) ? VIG_INNER_DISC : VIG_INNER_PORTRAIT;
    _cur_dsc = photo::acquire(_index);
    if (_cur_dsc) {
        lv_image_set_src(_photo, _cur_dsc);
    } else {
        // 空照片库不能把用户困在一个不存在的桌面工具入口里。
        // 启动器是功能入口；进入望后仍可下滑返回。
        lv_obj_add_flag(_photo, LV_OBJ_FLAG_HIDDEN);
        _hint = ui::mono_block(_stage, "No photos yet\nSwipe down for apps",
                               SN_FONT_MONO_S, BRONZE_D, 300);
        lv_obj_center(_hint);
    }

    /* ---- 层 1：晕影 ---- */
    build_vignette();

    /* ---- 层 1.5：Rim 分钟弧 ----
       寐版式里不显示时间，但一圈鎏金细弧走满即一小时。
       想知道几点就敲一下桌子，不想知道就当它是个装饰 */
    _rim = ui::arc(_stage, {R_RIM, 4, BRONZE, INK, true}, 0, 1);

    /* ---- 层 2：衬底 ----
       从透明到纯黑的竖向渐变，保证数字在任何照片上都读得清 */
    _scrim = lv_obj_create(_stage);
    lv_obj_set_size(_scrim, SCREEN, 190);
    lv_obj_align(_scrim, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(_scrim, 0, 0);
    lv_obj_set_style_radius(_scrim, 0, 0);
    lv_obj_set_style_pad_all(_scrim, 0, 0);
    lv_obj_set_style_bg_color(_scrim, c(INK), 0);
    lv_obj_set_style_bg_grad_color(_scrim, c(INK), 0);
    lv_obj_set_style_bg_grad_dir(_scrim, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(_scrim, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(_scrim, LV_OPA_90, 0);
    lv_obj_remove_flag(_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(_scrim, LV_OBJ_FLAG_CLICKABLE);

    /* ---- 层 3：时间 ---- */
    _time = ui::text(_stage, "--:--", SN_FONT_NUM_L, SILK);
    lv_obj_align(_time, LV_ALIGN_CENTER, 0, 118);

    _date = ui::text(_stage, "", SN_FONT_MONO_S, BRONZE);
    lv_obj_align(_date, LV_ALIGN_CENTER, 0, 168);
    lv_obj_set_style_opa(_date, LV_OPA_TRANSP, 0);

    _chord = ui::chord(_stage, "", BRONZE_D);
    lv_obj_add_flag(_chord, LV_OBJ_FLAG_HIDDEN);
    build_joy();

    /* ---- 层 4：外圈点 ----
       边牧的本职工作是 outrun —— 绕着羊群跑一个大弧把它们收拢。
       所以这个页面没有秒针，只有团团在跑外圈 */
    for (int i = 0; i < kTrailDots; i++) {
        lv_obj_t* t = lv_obj_create(_stage);
        const int sz = 5 - i / 2;
        lv_obj_set_size(t, sz, sz);
        lv_obj_set_style_radius(t, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(t, c(BRONZE_D), 0);
        lv_obj_set_style_bg_opa(t, 150 - i * 24, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        _trail[i] = t;
    }
    _dot = lv_obj_create(_stage);
    lv_obj_set_size(_dot, 7, 7);
    lv_obj_set_style_radius(_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_dot, c(BRONZE), 0);
    lv_obj_set_style_border_width(_dot, 0, 0);
    lv_obj_set_style_pad_all(_dot, 0, 0);
    lv_obj_remove_flag(_dot, LV_OBJ_FLAG_SCROLLABLE);

    apply_layout();
    tick_clock(true);
    ui::set_luma(ui::Luma::Quiet);
    mclog::tagInfo(getAppInfo().name, "on open, photo {} of {}", _index + 1, photo::count());
}

/*
 * 晕影用同心弧堆出来，不用带 alpha 的位图。三个好处：
 * 不占 flash、半径可运行时改（呼吸靠这个）、
 * 而且它天然属于"一切都是弧"的设计语言。
 */
void AppGaze::build_vignette()
{
    for (int i = 0; i < kVignetteRings; i++) {
        // 从内往外越来越不透明，最外一圈是纯黑 —— AMOLED 上像素直接熄灭，
        // 于是照片没有边缘，是悬浮在黑暗里的
        const float t = static_cast<float>(i) / (kVignetteRings - 1);
        const int opa = static_cast<int>(255.0f * t * t);  // 平方曲线，线性会看出台阶
        _vig[i] = ui::arc(_stage, {vig_radius(i, 0, _vig_inner), 3, INK, INK, false}, 0, 359.9f);
        lv_obj_set_style_arc_opa(_vig[i], opa, LV_PART_INDICATOR);
    }
}

/*
 * 三个版式：
 *
 *   寐  默认。团团蜷成的正圆本身就是钟面，上面不该压任何东西。
 *       想知道几点敲一下桌子就行。
 *   时  常显时间，数字锚在下三分之一，不压他的头。
 *   忆  好日子。外圈每一颗刻度是一张照片、一个日子，弦区写那天是什么日子。
 *       **不写年龄** —— 年龄是减法，日子是加法。环只占 300°，
 *       留 60° 是还没发生的好日子，所以它永远不会满，只会越来越密。
 */
void AppGaze::apply_layout()
{
    const bool sleep = (_layout == 0);
    const bool joy   = (_layout == 2);

    lv_obj_set_style_opa(_time,  sleep || joy ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_opa(_scrim, sleep ? LV_OPA_TRANSP : LV_OPA_90, 0);
    lv_obj_set_style_opa(_rim,   sleep ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(_time, SN_FONT_NUM_L, 0);
    lv_obj_set_style_opa(_photo, LV_OPA_COVER, 0);
    lv_obj_align(_time, LV_ALIGN_CENTER, 0, kTimeY);
    show_joy(joy);
}

/*
 * 忆的刻度环：一张照片一颗。
 *
 * 刻度只铺 300°，剩下 60° 空着 —— 那是还没发生的好日子。
 * 这一条不是装饰：一条铺满整圈的进度环暗示的是终点，而这一页要说的是
 * 「又多了一个好日子」。加照片只增不减，环只会越来越密。
 */
void AppGaze::build_joy()
{
    _joy_ring = ui::ticks(_stage, R_RIM, photo::count() > 0 ? photo::count() : 1,
                          9, 3, BRONZE_D);
    lv_obj_add_flag(_joy_ring, LV_OBJ_FLAG_HIDDEN);

    _joy_cap = ui::text(_stage, "", SN_FONT_CJK_L, SILK);
    // 弦区在 center+139，caption 必须让开，否则两行字叠在一起
    lv_obj_align(_joy_cap, LV_ALIGN_CENTER, 0, 96);
    lv_obj_add_flag(_joy_cap, LV_OBJ_FLAG_HIDDEN);
}

void AppGaze::show_joy(bool on)
{
    if (!_joy_ring || !_joy_cap) return;
    if (!on) {
        lv_obj_add_flag(_joy_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_joy_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_chord, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(_joy_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_joy_cap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_chord, LV_OBJ_FLAG_HIDDEN);
    ui::ticks_reset_color(_joy_ring, BRONZE_D);
    ui::tick_set_color(_joy_ring, _index, SILK);
    lv_label_set_text(_joy_cap, photo::caption_of(_index));
    // 大字写日子，弦区写日期 —— 跟预览里的层次一致
    lv_label_set_text(_chord, photo::date_of(_index));
    lv_obj_set_style_text_color(_chord, c(BRONZE_D), 0);
}

// 分钟进度：一圈走满即一小时。跟着外圈那颗点一起读
void AppGaze::tick_rim()
{
    if (_layout != 0) return;
    const auto t = GetHAL().getTimeHms();
    ui::arc_set_progress(_rim, (t.minute * 60.0f + t.second) / 3600.0f);
}

void AppGaze::tick_photo(uint32_t now)
{
    if (now - _t_photo < kPhotoPeriod) return;
    _t_photo = now;

    const float sec = now / 1000.0f;

    if (_cur_dsc) {
        // 漂移：只改 offset，纯 blit，不做任何重采样
        const float dx = kDriftAmp * std::sin(2 * kPi * sec / kDriftSecX);
        const float dy = kDriftAmp * std::sin(2 * kPi * sec / kDriftSecY);
        // 视差：照片朝倾斜的反方向走，才有"照片在窗后面"的错觉
        const int ox = static_cast<int>(photo::PAN + dx - _tilt_x * kTiltPhoto);
        const int oy = static_cast<int>(photo::PAN + dy - _tilt_y * kTiltPhoto);
        lv_image_set_offset_x(_photo, -std::clamp(ox, 0, photo::PAN * 2));
        lv_image_set_offset_y(_photo, -std::clamp(oy, 0, photo::PAN * 2));
    }

    // 呼吸：只动晕影半径，脏区就是外圈那一环
    const float br = std::sin(2 * kPi * sec * kBreathBpm / 60.0f);
    const int shift = static_cast<int>(br * kBreathPx);
    for (int i = 0; i < kVignetteRings; i++) {
        const int r = vig_radius(i, shift, _vig_inner);
        lv_obj_set_size(_vig[i], r * 2 + 3, r * 2 + 3);
        lv_obj_center(_vig[i]);
    }

    // 时间层朝倾斜的同方向轻移，与照片反向，视差才成立
    const int tx = static_cast<int>(_tilt_x * kTiltText);
    lv_obj_align(_time, LV_ALIGN_CENTER, tx, kTimeY);
}

void AppGaze::tick_dot(uint32_t now)
{
    if (now - _t_dot < kDotPeriod) return;
    _t_dot = now;

    const auto t = GetHAL().getTimeHms();
    const float frac = (t.second + (now % 1000) / 1000.0f) / 60.0f;
    const float deg = frac * 360.0f;

    ui::place_polar(_dot, R_RIM, deg);
    // 拖痕落在身后，像草地上的跑道
    for (int i = 0; i < kTrailDots; i++) {
        ui::place_polar(_trail[i], R_RIM, deg - (i + 1) * 2.4f);
    }
}

void AppGaze::tick_clock(bool force)
{
    const auto t = GetHAL().getTimeHms();
    if (!force && t.minute == _last_min) return;
    _last_min = t.minute;

    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", t.hour, t.minute);
    lv_label_set_text(_time, buf);

    const auto d = GetHAL().getDateYmd();
    char db[16];
    std::snprintf(db, sizeof(db), "%02d.%02d", d.month, d.day);
    lv_label_set_text(_date, db);
}

void AppGaze::swap_photo()
{
    if (photo::count() < 2 || _next_dsc) return;  // 上一次淡入还没完就别叠

    const int next = (_index + 1) % photo::count();
    const lv_image_dsc_t* dsc = photo::acquire(next);
    if (!dsc) return;

    // 交叉淡入：新图在上层从透明推到不透明，完事再交换指针
    lv_image_set_src(_photo_next, dsc);
    lv_image_set_offset_x(_photo_next, -photo::PAN);
    lv_image_set_offset_y(_photo_next, -photo::PAN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _photo_next);
    lv_anim_set_exec_cb(&a, [](void* o, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t*>(o), v, 0);
    });
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, kFadeMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    _next_dsc = _cur_dsc;   // 旧图留到淡入结束再释放
    _cur_dsc  = dsc;
    _index    = next;
    _t_swap   = GetHAL().millis();
    // 新图可能是另一种形态，晕影内缘跟着换
    _vig_inner = photo::is_disc(_index) ? VIG_INNER_DISC : VIG_INNER_PORTRAIT;
    if (_layout == 2) show_joy(true);   // 忆：换图时刻度和文案跟着走

    // 用定时器收尾，不要阻塞等待
    lv_timer_t* tm = lv_timer_create([](lv_timer_t* timer) {
        static_cast<AppGaze*>(lv_timer_get_user_data(timer))->onSwapDone();
        lv_timer_delete(timer);
    }, kFadeMs + 50, this);
    lv_timer_set_repeat_count(tm, 1);
}

void AppGaze::onSwapDone()
{
    if (!_photo) return;
    lv_image_set_src(_photo, _cur_dsc);
    lv_obj_set_style_opa(_photo_next, LV_OPA_TRANSP, 0);
    if (_next_dsc) {
        photo::release(_next_dsc);
        _next_dsc = nullptr;
    }
}

void AppGaze::onRunning()
{
    auto& hal = GetHAL();
    hal.updateButtonStates();
    if (_key && _key->update(false) == input::KeyEvent::GoHome) {
        close();
        return;
    }

    const uint32_t now = hal.millis();

    // 有请求时直接把守推到前台，不是 close 回 launcher ——
    // 那样用户还得自己去点开守，物理批准的主路径就断了
    if (sinan::route::yield_to_ward_if_needed()) return;

    hal.updateImuData();
    const auto& imu = hal.getImuData();
    // 低通，否则视差会抖
    _tilt_x = _tilt_x * 0.9f + imu.accelX * 0.1f;
    _tilt_y = _tilt_y * 0.9f + imu.accelY * 0.1f;

    const float g = std::sqrt(imu.accelX * imu.accelX + imu.accelY * imu.accelY +
                              imu.accelZ * imu.accelZ);
    if (g > TAP_G && now - _t_woke > 1500) {
        _t_woke = now;
        ui::set_luma(ui::Luma::Normal);
        LvglLockGuard lock;
        lv_obj_set_style_opa(_date, LV_OPA_COVER, 0);
        // 寐版式下平时不显示时间，敲一下才露出来
        if (_layout == 0) {
            lv_obj_set_style_opa(_time, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_grad_opa(_scrim, LV_OPA_90, 0);
        }
    }
    if (_t_woke && now - _t_woke > kWakeHold) {
        _t_woke = 0;
        ui::set_luma(ui::Luma::Quiet);
        LvglLockGuard lock;
        lv_obj_set_style_opa(_date, LV_OPA_TRANSP, 0);
        if (_layout == 0) {
            lv_obj_set_style_opa(_time, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_grad_opa(_scrim, LV_OPA_TRANSP, 0);
        }
    }

    if (hal.btnA.pressedFor(LONGPRESS_MS)) {
        _locked = !_locked;
        hal.vibrate(_locked ? 120 : 60, 80);
    } else if (hal.btnA.wasClicked()) {
        _layout = (_layout + 1) % 3;
        hal.vibrate(40, 60);
        LvglLockGuard lock;
        apply_layout();
    }

    const auto tp = hal.getTouchPoint();
    const bool down = tp.num > 0;
    if (down) {
        if (!_touch_was_down) {
            _touch_start_x = tp.x;
            _touch_start_y = tp.y;
        }
        _touch_last_x = tp.x;
        _touch_last_y = tp.y;
    } else if (_touch_was_down) {
        const int dx = _touch_last_x - _touch_start_x;
        const int dy = _touch_last_y - _touch_start_y;
        const int ax = std::abs(dx);
        const int ay = std::abs(dy);

        // 与官方状态栏一致：明确的向下滑动回到功能启动器。
        if (dy > kSwipeMin && ay > ax) {
            _touch_was_down = false;
            close();
            return;
        }

        // 左右滑或轻点均换下一张；空照片库时保持提示，不做假反馈。
        if (photo::count() > 0) {
            LvglLockGuard lock;
            swap_photo();
            hal.vibrate(30, 50);
        }
    }
    _touch_was_down = down;

    LvglLockGuard lock;
    tick_photo(now);
    tick_dot(now);
    tick_clock(false);
    tick_rim();

    if (!_locked && now - _t_swap > kSwapAfter) swap_photo();
}

void AppGaze::onClose()
{
    _key.reset();

    LvglLockGuard lock;
    if (_stage) lv_obj_delete(_stage);
    ui::root_release();
    _stage = _photo = _photo_next = _hint = _rim = _scrim = _time = _date = _dot = nullptr;
    _joy_ring = _joy_cap = _chord = nullptr;
    _vig.fill(nullptr);
    _trail.fill(nullptr);

    if (_cur_dsc)  { photo::release(_cur_dsc);  _cur_dsc = nullptr; }
    if (_next_dsc) { photo::release(_next_dsc); _next_dsc = nullptr; }
    ui::set_luma(ui::Luma::Normal);
}

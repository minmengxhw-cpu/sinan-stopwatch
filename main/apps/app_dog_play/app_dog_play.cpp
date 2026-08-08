#include "app_dog_play.h"

#include <M5GFX.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_log.h>
#include <cstddef>
#include <cstdio>

namespace {

extern const uint8_t puppy_start[] asm("_binary_2020_puppy_jpg_start");
extern const uint8_t puppy_end[] asm("_binary_2020_puppy_jpg_end");
extern const uint8_t curled_start[] asm("_binary_curled_jpg_start");
extern const uint8_t curled_end[] asm("_binary_curled_jpg_end");

struct DogPhoto {
    const uint8_t* start;
    const uint8_t* end;
    const char* name;
};

constexpr DogPhoto kPhotos[] = {
    {puppy_start, puppy_end, "PUPPY"},
    {curled_start, curled_end, "CURLED"},
};

constexpr int kPhotoCount = static_cast<int>(sizeof(kPhotos) / sizeof(kPhotos[0]));
constexpr int kSwipeMin = 45;

}  // namespace

AppDogPlay::AppDogPlay()
{
    static uint32_t launcher_color = 0x4FA88A;
    setAppInfo().name = "Dog Play";
    setAppInfo().icon = (void*)&icon_lucky_wheel;
    setAppInfo().userData = &launcher_color;
}

void AppDogPlay::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppDogPlay::onOpen()
{
    _index = 0;
    _play_count = 0;
    _touch_was_down = false;
    GetHAL().stopLvglUpdate();
    {
        LvglLockGuard lock;
        GetHAL().bootLogo.reset();
    }
    GetHAL().setBackLightBrightness(88, false);
    render("READY");
}

void AppDogPlay::render(const char* action)
{
    auto& display = GetHAL().getDisplay();
    const auto& photo = kPhotos[_index];
    const size_t bytes = static_cast<size_t>(photo.end - photo.start);
    constexpr int side = 560;
    const int origin = (display.width() - side) / 2;

    display.startWrite();
    display.fillScreen(TFT_BLACK);
    const bool ok = display.drawJpg(photo.start, bytes,
                                    origin, origin, side, side,
                                    0, 0, 0.0f, 0.0f,
                                    datum_t::middle_center);
    display.fillRect(0, display.height() - 58, display.width(), 58, TFT_BLACK);
    display.drawCircle(display.width() / 2, display.height() / 2,
                       display.width() / 2 - 4, 0x4FA88A);
    display.setTextFont(2);
    display.setTextDatum(textdatum_t::middle_center);
    display.setTextColor(0xF2EDE1, TFT_BLACK);
    display.drawString(action, display.width() / 2, display.height() - 35);
    display.setTextColor(0x6E5C3A, TFT_BLACK);
    char footer[40];
    std::snprintf(footer, sizeof(footer), "%s  ·  PLAY %02d", photo.name, _play_count);
    display.drawString(footer, display.width() / 2, display.height() - 14);
    display.endWrite();
    display.display();

    ESP_LOGI("DOGPLAY", "photo=%s action=%s count=%d result=%s",
             photo.name, action, _play_count, ok ? "OK" : "FAIL");
}

void AppDogPlay::play(const char* action)
{
    ++_play_count;
    GetHAL().vibrate(70, 75);
    render(action);
}

void AppDogPlay::onRunning()
{
    auto& hal = GetHAL();
    hal.feedTheDog();
    hal.updateButtonStates();

    if (hal.btnA.wasClicked()) {
        play("PET");
    } else if (hal.btnB.wasClicked()) {
        _index = (_index + 1) % kPhotoCount;
        play("SWITCH");
    }

    const auto touch = hal.getTouchPoint();
    const bool down = touch.num > 0;
    if (down) {
        if (!_touch_was_down) {
            _touch_start_x = touch.x;
            _touch_start_y = touch.y;
        }
        _touch_last_x = touch.x;
        _touch_last_y = touch.y;
    } else if (_touch_was_down) {
        const int dx = _touch_last_x - _touch_start_x;
        const int dy = _touch_last_y - _touch_start_y;
        if (dx <= -kSwipeMin) {
            _index = (_index + 1) % kPhotoCount;
            play("CHASE");
        } else if (dx >= kSwipeMin) {
            _index = (_index + kPhotoCount - 1) % kPhotoCount;
            play("COME BACK");
        } else if (dy < -kSwipeMin) {
            play("JUMP");
        } else {
            play("PET");
        }
    }
    _touch_was_down = down;

    if (hal.btnA.isHolding() && hal.btnB.isHolding()) close();
    hal.delay(10);
}

void AppDogPlay::onClose()
{
    GetHAL().startLvglUpdate();
    mclog::tagInfo(getAppInfo().name, "on close");
}

#include "app_dog_photo.h"

#include <M5GFX.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <esp_log.h>
#include <cstddef>

namespace {

extern const uint8_t puppy_start[] asm("_binary_2020_puppy_jpg_start");
extern const uint8_t puppy_end[] asm("_binary_2020_puppy_jpg_end");
extern const uint8_t curled_start[] asm("_binary_curled_jpg_start");
extern const uint8_t curled_end[] asm("_binary_curled_jpg_end");

struct DogPhoto {
    const uint8_t* start;
    const uint8_t* end;
    const char* name;
    int render_side;
};

constexpr DogPhoto kPhotos[] = {
    {puppy_start, puppy_end, "2020 / PUPPY", 500},
    {curled_start, curled_end, "CURLED / SLEEP", 620},
};

constexpr int kPhotoCount = static_cast<int>(sizeof(kPhotos) / sizeof(kPhotos[0]));
constexpr int kSwipeMin = 45;

}  // namespace

AppDogPhoto::AppDogPhoto()
{
    static uint32_t launcher_color = 0xC8A96E;
    setAppInfo().name = "Photos";
    setAppInfo().icon = (void*)&icon_photos;
    setAppInfo().userData = &launcher_color;
}

void AppDogPhoto::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create, embedded photos={}", kPhotoCount);
}

void AppDogPhoto::onOpen()
{
    _index = 0;
    _touch_was_down = false;
    _touch_start_x = 0;
    _touch_last_x = 0;

    // The photo page owns the panel for the duration of the app.  LVGL is
    // paused so its old image decoder cannot paint over the direct JPEG frame.
    GetHAL().stopLvglUpdate();
    {
        LvglLockGuard lock;
        GetHAL().bootLogo.reset();
    }
    GetHAL().setBackLightBrightness(80, false);
    render();
}

void AppDogPhoto::render()
{
    auto& display = GetHAL().getDisplay();
    const auto& photo = kPhotos[_index];
    const size_t bytes = static_cast<size_t>(photo.end - photo.start);
    const int side = photo.render_side;
    const int origin = (display.width() - side) / 2;

    display.startWrite();
    display.fillScreen(TFT_BLACK);
    const bool ok = display.drawJpg(photo.start, bytes,
                                    origin, origin, side, side,
                                    0, 0, 0.0f, 0.0f,
                                    datum_t::middle_center);

    // CC's Sinan language: one quiet bronze rim, one warm label, no card UI.
    display.drawCircle(display.width() / 2, display.height() / 2,
                       display.width() / 2 - 4, 0xC8A96E);
    display.setTextDatum(textdatum_t::bottom_center);
    display.setTextFont(2);
    display.setTextColor(0xF2EDE1, TFT_BLACK);
    display.drawString(photo.name, display.width() / 2, display.height() - 12);
    display.setTextDatum(textdatum_t::top_center);
    display.setTextColor(0x6E5C3A, TFT_BLACK);
    display.drawString("A PREV   ·   B NEXT", display.width() / 2, 10);
    display.endWrite();
    display.display();

    ESP_LOGI("DOGPHOTO", "render index=%d/%d name=%s bytes=%u side=%d result=%s",
             _index + 1, kPhotoCount, photo.name,
             static_cast<unsigned>(bytes), side, ok ? "OK" : "FAIL");
}

void AppDogPhoto::move(int delta)
{
    _index = (_index + delta + kPhotoCount) % kPhotoCount;
    GetHAL().vibrate(25, 55);
    render();
}

void AppDogPhoto::onRunning()
{
    auto& hal = GetHAL();
    hal.feedTheDog();
    hal.updateButtonStates();

    if (hal.btnA.wasClicked()) {
        move(-1);
    } else if (hal.btnB.wasClicked()) {
        move(1);
    }

    const auto touch = hal.getTouchPoint();
    const bool down = touch.num > 0;
    if (down) {
        if (!_touch_was_down) _touch_start_x = touch.x;
        _touch_last_x = touch.x;
    } else if (_touch_was_down) {
        const int dx = _touch_last_x - _touch_start_x;
        if (dx <= -kSwipeMin) move(1);
        else if (dx >= kSwipeMin) move(-1);
        else move(1);
    }
    _touch_was_down = down;

    if (hal.btnA.isHolding() && hal.btnB.isHolding()) {
        close();
    }
    hal.delay(10);
}

void AppDogPhoto::onClose()
{
    GetHAL().startLvglUpdate();
    mclog::tagInfo(getAppInfo().name, "on close");
}

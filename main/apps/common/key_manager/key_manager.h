/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>

namespace input {

enum class KeyEvent {
    None,
    GoHome,
    GoPrevious,
    GoNext,
};

class KeyManager {
public:
    explicit KeyManager(bool touchHome = false) : _touch_home_enabled(touchHome) {}
    const KeyEvent& update(bool updateButtonStates = true);
    const KeyEvent& getEvent() const
    {
        return _event;
    }

private:
    KeyEvent _event       = KeyEvent::None;
    bool _go_home_latched = false;
    bool _touch_home_enabled = false;
    bool _touch_was_down = false;
    int _touch_start_x = -1;
    int _touch_start_y = -1;
    int _touch_last_x = -1;
    int _touch_last_y = -1;
};

}  // namespace input

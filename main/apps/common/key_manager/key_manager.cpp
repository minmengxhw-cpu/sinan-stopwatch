/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "key_manager.h"
#include <cstdlib>

using namespace input;

const KeyEvent& KeyManager::update(bool updateButtonStates)
{
    if (updateButtonStates) {
        GetHAL().updateButtonStates();
    }

    _event = KeyEvent::None;

    if (_touch_home_enabled) {
        const auto tp = GetHAL().getTouchPoint();
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
            if (dy > 80 && std::abs(dy) > std::abs(dx)) {
                _event = KeyEvent::GoHome;
            }
        }
        _touch_was_down = down;
        if (_event == KeyEvent::GoHome) return _event;
    }

    if (GetHAL().btnA.isHolding() && GetHAL().btnB.isHolding()) {
        if (!_go_home_latched) {
            _event           = KeyEvent::GoHome;
            _go_home_latched = true;
        }
    } else if (GetHAL().btnA.wasClicked()) {
        _event = KeyEvent::GoPrevious;
    } else if (GetHAL().btnB.wasClicked()) {
        _event = KeyEvent::GoNext;
    } else if (GetHAL().btnA.isReleased() && GetHAL().btnB.isReleased()) {
        _go_home_latched = false;
    }

    return _event;
}

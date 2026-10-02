// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/protocol.hpp"

namespace touchpad {
// Input Test must be able to display Plus/Minus without exiting on a tap.
class TestExitChord {
    bool held_ = false;
    uint64_t since_ = 0, previous_ = 0;
public:
    bool update(uint64_t buttons, uint64_t now) {
        constexpr uint64_t chord = ButtonPlus | ButtonMinus;
        if ((buttons & chord) != chord) { held_ = false; return false; }
        if (!held_ || now < previous_ || now-previous_ >= TimeoutMs) {
            since_ = now;
            held_ = true;
        }
        previous_ = now;
        return now-since_ >= 2000;
    }
};
}

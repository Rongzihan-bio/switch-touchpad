// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace touchpad {
struct SwipeSettings {
    bool enabled = false;
    bool right = false;
    int offset = 0;
};
// Same edge/target/time bounds as Ultrahand; only a new, single finger may
// start a gesture. Never turn a game drag or an old pre-sleep touch into a swipe.
class EdgeSwipe {
    bool released_ = false, tracking_ = false;
    uint32_t owner_ = 0;
    uint64_t start_ = 0, last_ = 0;
public:
    void cancel() { released_ = tracking_ = false; }
    bool update(const SwipeSettings& settings, bool available, bool hidden,
                uint32_t count, uint32_t id, int x, int y, uint64_t now) {
        if (now < last_ || now-last_ > 250) cancel();
        last_ = now;
        if (!available || !hidden || !settings.enabled || count > 1 ||
            settings.offset < 0 || settings.offset > 200) { cancel(); return false; }
        if (count == 0) { released_ = true; tracking_ = false; return false; }
        if (x < 0 || x >= 1280 || y < 0 || y >= 720) { cancel(); return false; }
        const int inward = settings.right ? 1280-x : x;
        if (released_) {
            released_ = false;
            tracking_ = inward <= 16+settings.offset;
            owner_ = id;
            start_ = now;
        }
        if (!tracking_) return false;
        if (id != owner_ || now-start_ > 150) { cancel(); return false; }
        if (inward > 100+settings.offset) { cancel(); return true; }
        return false;
    }
};
}

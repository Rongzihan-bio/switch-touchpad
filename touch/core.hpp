// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/protocol.hpp"
#include <array>

namespace touchpad {
struct Point { uint32_t id = 0; float x = 0, y = 0; };
struct Touches { std::array<Point, 16> points{}; uint32_t count = 0; };
struct Circle { float x, y, radius; };
struct Layout {
    Circle stick;
    std::array<Circle, 4> buttons; // A B X Y
    static Layout make(const Settings& settings);
};
bool valid(const Touches& touches);
class Mapper {
    bool captured_ = false;
    uint32_t owner_ = 0;
    Touches previous_{};
public:
    void reset();
    PadState update(const Touches& touches, const Settings& settings);
};
struct Sample {
    Touches touches{};
    uint64_t counter = 0;
    uint64_t application_pid = 0;
    bool available = false;
    bool home = false, sleep = false, tesla = false, docked = false, clock_gap = false;
};
class Engine {
    Status state_{};
    Mapper mapper_{};
    uint64_t last_tick_ = 0, last_fresh_ = 0, counter_ = 0, deadline_ = 0;
    bool have_counter_ = false, pair_ = false;
public:
    const Status& state() const { return state_; }
    void setSettings(const Settings& s);
    Error prepare(bool pair, uint64_t pid, uint64_t now);
    Error commit(uint64_t token, uint64_t now);
    void stop(Reason why, bool off = false);
    void tick(const Sample& sample, uint64_t now);
};
}

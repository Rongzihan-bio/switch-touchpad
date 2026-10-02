// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch/core.hpp"
#include "touch/arcade.hpp"
#include <algorithm>
#include <cmath>

namespace touchpad {
static bool inside(const Circle& c, const Point& p) {
    return std::hypot(p.x - c.x, p.y - c.y) <= c.radius;
}
Layout Layout::make(const Settings& s) {
    const float f = s.scale / 100.0f;
    return {{180, 540, 100*f}, {{{1100+75*f,540,36*f}, {1100,540+75*f,36*f},
                               {1100,540-75*f,36*f}, {1100-75*f,540,36*f},
                               {120,350,36*f}, {240,350,36*f},
                               {1040,350,36*f}, {1160,350,36*f},
                               {540,650,32*f}, {740,650,32*f}}}};
}
bool valid(const Touches& t) {
    if (t.count > t.points.size()) return false;
    for (uint32_t i = 0; i < t.count; ++i) {
        const auto& p = t.points[i];
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.x >= 1280 || p.y < 0 || p.y >= 720) return false;
        for (uint32_t j = 0; j < i; ++j) if (p.id == t.points[j].id) return false;
    }
    return true;
}
void Mapper::reset() { captured_ = false; previous_ = {}; }
PadState Mapper::update(const Touches& t, const Settings& s) {
    PadState out{};
    if (!valid(t) || !valid(s)) { reset(); return out; }
    const auto layout = Layout::make(s);
    const Point* owner = nullptr;
    if (captured_) {
        for (uint32_t i = 0; i < t.count; ++i) if (t.points[i].id == owner_) owner = &t.points[i];
        if (!owner) captured_ = false;
    }
    if (!captured_) {
        for (uint32_t i = 0; i < t.count; ++i) {
            const auto& p = t.points[i];
            bool newFinger = true;
            for (uint32_t j = 0; j < previous_.count; ++j) if (previous_.points[j].id == p.id) newFinger = false;
            if (newFinger && inside(layout.stick, p)) { captured_ = true; owner_ = p.id; owner = &p; break; }
        }
    }
    if (owner) {
        const float x = (owner->x - layout.stick.x) / layout.stick.radius;
        const float y = (layout.stick.y - owner->y) / layout.stick.radius;
        const float length = std::hypot(x, y), dead = s.deadzone / 100.0f;
        if (s.stick_mode == StickMode::Arcade8) {
            applyArcadeDirection(out,arcadeDirection(x,y,s.arcade_deadzone/100.0f),s.arcade_output);
        } else if (length > dead) {
            const float magnitude = (std::min(length, 1.0f) - dead) / (1.0f - dead);
            out.lx = static_cast<int32_t>(std::lround(x / length * magnitude * 32767));
            out.ly = static_cast<int32_t>(std::lround(y / length * magnitude * 32767));
        }
    }
    for (uint32_t i = 0; i < t.count; ++i) {
        if (captured_ && t.points[i].id == owner_) continue;
        for (size_t j = 0; j < layout.buttons.size(); ++j)
            if (inside(layout.buttons[j], t.points[i])) out.buttons |= Layout::ButtonBits[j];
    }
    previous_ = t;
    return out;
}
void Engine::setSettings(const Settings& s) {
    if (!valid(s)) return;
    stop(Reason::SettingsChanged);
    state_.settings = s;
}
void Engine::stop(Reason why, bool off) {
    state_.mode = off ? Mode::Off : Mode::Paused;
    state_.reason = why;
    state_.pad = {};
    ++state_.token; // invalidate delayed commits from an earlier menu session
    mapper_.reset();
    pair_ = false;
}
Error Engine::prepare(bool pair, uint64_t pid, uint64_t now) {
    stop(Reason::None);
    if (!pair && pid == 0) return Error::NoApplication;
    pair_ = pair;
    state_.application_pid = pid;
    state_.mode = pair ? Mode::PairPrepared : Mode::Prepared;
    deadline_ = now + 5000;
    last_tick_ = last_fresh_ = now;
    have_counter_ = false;
    return Error::Ok;
}
Error Engine::commit(uint64_t token, uint64_t now) {
    if (token != state_.token || (state_.mode != Mode::Prepared && state_.mode != Mode::PairPrepared) || now >= deadline_) {
        return Error::InvalidTransition;
    }
    state_.mode = Mode::WaitingTouch;
    deadline_ = now + 5000;
    return Error::Ok;
}
void Engine::tick(const Sample& s, uint64_t now) {
    ++state_.sequence;
    state_.touch_count = s.touches.count;
    const bool running = state_.mode == Mode::Active || state_.mode == Mode::WaitingTouch ||
                         state_.mode == Mode::Prepared || state_.mode == Mode::PairPrepared || state_.mode == Mode::Pairing;
    if (!running) { last_tick_ = now; return; }
    if (now < last_tick_ || now - last_tick_ >= TimeoutMs || s.clock_gap) { stop(Reason::ClockGap); return; }
    last_tick_ = now;
    if (s.home || s.sleep || s.tesla || s.docked) {
        stop(s.home ? Reason::Home : s.sleep ? Reason::Sleep : s.tesla ? Reason::Tesla : Reason::Docked); return;
    }
    if (!pair_ && (!s.application_pid || s.application_pid != state_.application_pid)) { stop(Reason::ApplicationChanged); return; }
    if (!valid(s.touches)) { stop(Reason::InvalidInput); return; }
    if (s.available && (!have_counter_ || s.counter != counter_)) {
        if (have_counter_ && s.counter < counter_) { stop(Reason::StaleInput); return; }
        last_fresh_ = now; counter_ = s.counter; have_counter_ = true;
    }
    state_.sample_age_ms = static_cast<uint32_t>(now - last_fresh_);
    if (now - last_fresh_ >= TimeoutMs) { stop(Reason::StaleInput); return; }
    if (state_.mode == Mode::Prepared || state_.mode == Mode::PairPrepared || state_.mode == Mode::WaitingTouch) {
        if (now >= deadline_) { stop(Reason::Timeout); return; }
    }
    if (state_.mode == Mode::WaitingTouch && s.available && s.touches.count == 0) {
        mapper_.reset();
        state_.mode = pair_ ? Mode::Pairing : Mode::Active;
        deadline_ = now + 400;
    }
    if (state_.mode == Mode::Active && s.available) state_.pad = mapper_.update(s.touches, state_.settings);
    if (state_.mode == Mode::Pairing) {
        if (now >= deadline_) { stop(Reason::PairComplete); return; }
        state_.pad = {ButtonL | ButtonR, 0, 0, 0, 0};
    }
}
const char* modeName(Mode m) {
    constexpr const char* names[] = {"Off", "Prepared", "Lift fingers", "Active", "Pair ready", "Pairing", "Paused", "Fault"};
    return static_cast<unsigned>(m) < 8 ? names[static_cast<unsigned>(m)] : "Unknown";
}
const char* reasonName(Reason r) {
    constexpr const char* names[] = {"None", "User", "HOME", "Sleep", "Tesla", "Application changed", "Stale touch",
        "Timeout", "Invalid input", "Controller error", "Display error", "Pair complete", "Settings changed", "Docked", "Clock gap"};
    return static_cast<unsigned>(r) < 16 ? names[static_cast<unsigned>(r)] : "Unknown";
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace touchpad {
constexpr uint32_t ProtocolVersion = 2;
constexpr const char* ServiceName = "touchpad";
constexpr const char* Version = "1.1.0-beta.1";
constexpr uint64_t TimeoutMs = 250;
constexpr uint64_t RenderDelayNoticeMs = 250;
constexpr uint64_t ButtonA = 1ULL << 0, ButtonB = 1ULL << 1;
constexpr uint64_t ButtonX = 1ULL << 2, ButtonY = 1ULL << 3;
constexpr uint64_t ButtonL = 1ULL << 6, ButtonR = 1ULL << 7;
constexpr uint64_t TouchButtons = ButtonA | ButtonB | ButtonX | ButtonY;
constexpr uint64_t ButtonLeft = 1ULL << 12, ButtonUp = 1ULL << 13;
constexpr uint64_t ButtonRight = 1ULL << 14, ButtonDown = 1ULL << 15;
constexpr uint64_t DirectionButtons = ButtonLeft | ButtonUp | ButtonRight | ButtonDown;

enum class StickMode : uint32_t { Analog, Arcade8 };
enum class ArcadeOutput : uint32_t { Dpad, LeftStick };

struct Settings {
    uint32_t opacity = 40;
    uint32_t scale = 100;
    uint32_t deadzone = 15;
    uint32_t reserved = 0;
    StickMode stick_mode = StickMode::Analog;
    uint32_t arcade_deadzone = 20;
    ArcadeOutput arcade_output = ArcadeOutput::Dpad;
    uint32_t reserved2 = 0;
};
inline bool valid(const Settings& s) {
    return s.opacity >= 10 && s.opacity <= 80 && s.scale >= 75 && s.scale <= 125 &&
           s.deadzone >= 5 && s.deadzone <= 30 && s.reserved == 0 && s.reserved2 == 0 &&
           static_cast<uint32_t>(s.stick_mode) <= static_cast<uint32_t>(StickMode::Arcade8) &&
           s.arcade_deadzone >= 5 && s.arcade_deadzone <= 30 &&
           static_cast<uint32_t>(s.arcade_output) <= static_cast<uint32_t>(ArcadeOutput::LeftStick);
}
enum class Mode : uint32_t { Off, Prepared, WaitingTouch, Active, PairPrepared, Pairing, Paused, Fault };
enum class Reason : uint32_t {
    None, User, Home, Sleep, Tesla, ApplicationChanged, StaleInput, Timeout,
    InvalidInput, Backend, Renderer, PairComplete, SettingsChanged, Docked, ClockGap
};
enum class Command : uint32_t { Status, Configure, Prepare, Commit, Stop, Pause, PreparePair };
enum class Error : uint32_t { Ok, Protocol, InvalidSettings, InvalidTransition, NoApplication, Unavailable, Storage };
struct PadState {
    uint64_t buttons = 0;
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;
};
inline bool valid(const PadState& p, bool pairing = false) {
    const uint64_t mask = pairing ? ButtonL | ButtonR : TouchButtons | DirectionButtons;
    return !(p.buttons & ~mask) && p.lx >= -32767 && p.lx <= 32767 &&
           p.ly >= -32767 && p.ly <= 32767 && p.rx == 0 && p.ry == 0 &&
           (!pairing || (p.lx == 0 && p.ly == 0)) &&
           (p.buttons & (ButtonLeft | ButtonRight)) != (ButtonLeft | ButtonRight) &&
           (p.buttons & (ButtonUp | ButtonDown)) != (ButtonUp | ButtonDown);
}
struct Request {
    uint32_t version = ProtocolVersion;
    uint32_t size = sizeof(Request);
    Command command = Command::Status;
    uint32_t reserved = 0;
    uint64_t token = 0;
    Settings settings{};
};
struct Status {
    uint32_t version = ProtocolVersion;
    uint32_t size = sizeof(Status);
    Error error = Error::Ok;
    Mode mode = Mode::Off;
    Reason reason = Reason::None;
    uint32_t backend_result = 0;
    Settings settings{};
    PadState pad{};
    uint64_t token = 0;
    uint64_t sequence = 0;
    uint64_t application_pid = 0;
    uint32_t connected = 0;
    uint32_t renderer_ready = 0;
    uint32_t hos_version = 0;
    uint32_t touch_count = 0;
    uint32_t sample_age_ms = 0;
    uint32_t input_period_us = 0;
    uint32_t render_period_us = 0;
    uint32_t reserved = 0;
};
inline bool valid(const Request& r) {
    return r.version == ProtocolVersion && r.size == sizeof(Request) && r.reserved == 0 &&
           static_cast<uint32_t>(r.command) <= static_cast<uint32_t>(Command::PreparePair);
}
static_assert(sizeof(Settings) == 32 && sizeof(Request) == 56 && sizeof(PadState) == 24);
static_assert(sizeof(Status) == 136 && std::is_trivially_copyable_v<Status>);
const char* modeName(Mode mode);
const char* reasonName(Reason reason);
}

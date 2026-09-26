// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/protocol.hpp"
#include <cmath>
#include <cstdint>

namespace touchpad {
// Number-pad notation, viewed with up at the top. Five is neutral.
enum class Direction : uint32_t {
    DownLeft=1, Down=2, DownRight=3, Left=4, Neutral=5,
    Right=6, UpLeft=7, Up=8, UpRight=9
};
struct DirectionAxes { int x, y; };
constexpr DirectionAxes directionAxes(Direction direction) {
    switch (direction) {
        case Direction::DownLeft: return {-1,-1};
        case Direction::Down: return {0,-1};
        case Direction::DownRight: return {1,-1};
        case Direction::Left: return {-1,0};
        case Direction::Right: return {1,0};
        case Direction::UpLeft: return {-1,1};
        case Direction::Up: return {0,1};
        case Direction::UpRight: return {1,1};
        default: return {0,0};
    }
}
// Normalized displacement: one unit is the outer radius; positive Y is up.
// Every direction has a 45-degree sector. The exact boundary belongs to the
// counterclockwise sector. No time filtering or synthetic intermediate input.
inline Direction arcadeDirection(float x, float y, float deadzone) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(deadzone) ||
        deadzone<0 || deadzone>=1 || std::hypot(x,y)<=deadzone) return Direction::Neutral;
    constexpr float pi=3.14159265358979323846f;
    const float angle=std::atan2(y,x);
    const int sector=static_cast<int>(std::floor((angle+pi/8)/(pi/4)));
    constexpr Direction directions[] = {
        Direction::Right, Direction::UpRight, Direction::Up, Direction::UpLeft,
        Direction::Left, Direction::DownLeft, Direction::Down, Direction::DownRight
    };
    return directions[(sector+8)%8];
}
constexpr uint64_t directionButtons(Direction direction) {
    const auto axes=directionAxes(direction);
    return (axes.x<0 ? ButtonLeft : axes.x>0 ? ButtonRight : 0) |
           (axes.y<0 ? ButtonDown : axes.y>0 ? ButtonUp : 0);
}
inline void applyArcadeDirection(PadState& pad, Direction direction, ArcadeOutput output) {
    pad.buttons &= ~DirectionButtons;
    pad.lx=pad.ly=0;
    if (output==ArcadeOutput::Dpad) pad.buttons |= directionButtons(direction);
    else {
        const auto axes=directionAxes(direction);
        // Full radial magnitude, including diagonals; preserve the circular
        // range used by the existing analog mapper.
        const int magnitude=axes.x && axes.y ? 23170 : 32767;
        pad.lx=axes.x*magnitude; pad.ly=axes.y*magnitude;
    }
}
inline Direction padDirection(const PadState& pad) {
    if (pad.buttons & DirectionButtons) {
        const int x=!!(pad.buttons & ButtonRight)-!!(pad.buttons & ButtonLeft);
        const int y=!!(pad.buttons & ButtonUp)-!!(pad.buttons & ButtonDown);
        return arcadeDirection(static_cast<float>(x),static_cast<float>(y),0);
    }
    return arcadeDirection(static_cast<float>(pad.lx),static_cast<float>(pad.ly),0);
}
}

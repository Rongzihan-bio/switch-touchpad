// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "touch/core.hpp"
namespace touchpad {
// Linear RGBA4444 (R in the low nibble), stride in pixels.
void draw(uint16_t* pixels, uint32_t stride, const Settings& settings, const PadState& pad, bool visible, unsigned divisor = 1);
size_t tiledOffset(unsigned x, unsigned y, unsigned strideBytes);
}

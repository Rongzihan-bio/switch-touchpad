// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>

namespace touchpad {
// libtesla StepTrackBar reads, writes and reports step indices, not percent.
struct BrightnessScale {
    static constexpr unsigned Steps = 21;
    static constexpr unsigned PercentPerStep = 5;
    static constexpr unsigned toPercent(unsigned step) {
        return std::min(step,Steps-1)*PercentPerStep;
    }
    static constexpr uint8_t toStep(unsigned percent) {
        return static_cast<uint8_t>((std::min(percent,100U)+PercentPerStep/2)/PercentPerStep);
    }
};
}

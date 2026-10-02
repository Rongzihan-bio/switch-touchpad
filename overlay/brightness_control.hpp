// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace touchpad {
enum class BrightnessStage { None, Open, Read, Set, Apply, Save };
// Device owns the service session. Reads never change automatic brightness,
// power state or saved settings; persisting is a separate user action.
template<class Device>
class BrightnessControl {
    Device& device_;
    bool check(uint32_t rc, BrightnessStage operation) {
        result = rc;
        stage = rc ? operation : BrightnessStage::None;
        return rc == 0;
    }
public:
    explicit BrightnessControl(Device& device) : device_(device) {}
    bool available = false, automatic = false, unsaved = false;
    unsigned percent = 0;
    uint32_t result = 0;
    BrightnessStage stage = BrightnessStage::None;
    bool read() {
        available = false;
        if (!check(device_.open(),BrightnessStage::Open)) return false;
        float value = 0; bool autoValue = false;
        if (!check(device_.read(value,autoValue),BrightnessStage::Read)) return false;
        if (!std::isfinite(value) || value < 0 || value > 1)
            return check(Device::InvalidValue,BrightnessStage::Read);
        percent = static_cast<unsigned>(std::lround(value*100));
        automatic = autoValue;
        available = true;
        return true;
    }
    bool set(int requested) {
        // Never write a made-up default after a service/read failure.
        if (!available && !read()) return false;
        const unsigned target = static_cast<unsigned>(std::clamp(requested,5,100));
        if (!check(device_.set(target/100.0f),BrightnessStage::Set)) return false;
        unsaved = true;
        available = false;
        if (!check(device_.apply(),BrightnessStage::Apply)) return false;
        return read(); // show the setting actually accepted by the service
    }
    bool save() {
        if (!available || (result && stage != BrightnessStage::Save)) return false;
        if (!check(device_.save(),BrightnessStage::Save)) return false;
        unsaved = false;
        return true;
    }
};
}

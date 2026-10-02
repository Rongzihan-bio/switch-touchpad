// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <switch.h>

namespace touchpad {
class BrightnessService {
    bool ready_ = false;
public:
    static constexpr uint32_t InvalidValue = MAKERESULT(Module_Libnx,LibnxError_BadInput);
    ~BrightnessService() { close(); }
    Result open() {
        if (ready_) return 0;
        Result rc = smInitialize();
        if (R_FAILED(rc)) return rc;
        rc = lblInitialize();
        smExit();
        ready_ = R_SUCCEEDED(rc);
        return rc;
    }
    void close() { if (ready_) lblExit(); ready_ = false; }
    Result read(float& value, bool& automatic) {
        Result rc = lblGetCurrentBrightnessSetting(&value);
        if (R_SUCCEEDED(rc)) rc = lblIsAutoBrightnessControlEnabled(&automatic);
        return rc;
    }
    Result set(float value) { return lblSetCurrentBrightnessSetting(value); }
    Result apply() { return lblApplyCurrentBrightnessSettingToBacklight(); }
    Result save() { return lblSaveCurrentSetting(); }
};
}

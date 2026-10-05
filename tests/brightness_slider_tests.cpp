// SPDX-License-Identifier: GPL-2.0-or-later
#include "tesla-step-trackbar.hpp"
#include "overlay/brightness_control.hpp"
#include "overlay/brightness_scale.hpp"
#include <cassert>
#include <cstdio>

using namespace touchpad;
struct Device {
    static constexpr uint32_t InvalidValue=1;
    float value=.73f;
    unsigned sets=0;
    uint32_t open() { return 0; }
    uint32_t read(float& out,bool& automatic) { out=value; automatic=true; return 0; }
    uint32_t set(float in) { value=in; ++sets; return 0; }
    uint32_t apply() { return 0; }
    uint32_t save() { return 0; }
};
int main() {
    Device device; BrightnessControl control(device);
    tsl::elm::StepTrackBar slider("",BrightnessScale::Steps);
    auto sync=[&] { slider.setProgress(BrightnessScale::toStep(control.percent)); };
    slider.setValueChangedListener([&](u8 step) {
        assert(control.set(BrightnessScale::toPercent(step)));
        sync(); // exercise the same readback feedback as the real page
    });
    assert(control.read()); sync();
    assert(device.sets==0 && control.percent==73 && slider.getProgress()==15);
    // Opening/reopening at arbitrary system values must never write brightness.
    for (unsigned percent=0;percent<=100;++percent) {
        device.value=percent/100.0f;
        assert(control.read()); sync();
        assert(control.percent==percent && device.sets==0);
        const int shown=static_cast<int>(BrightnessScale::toPercent(slider.getProgress()));
        assert(std::abs(shown-static_cast<int>(percent))<=2);
    }
    auto touch=[&](int x) { return slider.onTouch(tsl::elm::TouchEvent::Hold,x,40,60,40,60,40); };
    // Repeated full sweeps catch the beta4 feedback bug: far right wrote 20%
    // and then interpreted 20% as step 20, falsely drawing a full slider.
    for (unsigned sweep=0;sweep<50;++sweep) {
        for (unsigned step=0;step<BrightnessScale::Steps;++step) {
            assert(touch(60+static_cast<int>(step)*10));
            assert(control.percent==std::max(step*5,5U));
            assert(slider.getProgress()==std::max(step,1U));
        }
        assert(control.percent==100 && device.value==1.0f);
        for (int step=20;step>=0;--step) {
            assert(touch(60+step*10));
            assert(control.percent==static_cast<unsigned>(std::max(step*5,5)));
        }
        assert(touch(400) && control.percent==100); // drag beyond right edge
        assert(touch(-100) && control.percent==5); // below lower bound
        assert(control.read()); sync(); // refresh after an actual adjustment
    }
    auto key=[&](u64 held) { return slider.handleInput(held,held,{},{},{}); };
    for (unsigned run=0;run<20;++run) {
        key(0);
        for (unsigned frame=0;frame<200;++frame) key(HidNpadButton_AnyRight);
        assert(control.percent==100 && slider.getProgress()==20);
        key(0);
        for (unsigned frame=0;frame<200;++frame) key(HidNpadButton_AnyLeft);
        assert(control.percent==5 && slider.getProgress()==1);
    }
    const auto writes=device.sets;
    key(0); key(HidNpadButton_AnyLeft|HidNpadButton_AnyRight);
    assert(device.sets==writes);
    std::puts("PASS: shipped libtesla slider touch/key input, 100% endpoint, repeated sweeps, readback feedback and read-only reopen");
}

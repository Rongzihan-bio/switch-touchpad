// SPDX-License-Identifier: GPL-2.0-or-later
#include "overlay/brightness_control.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>

using namespace touchpad;
struct Device {
    static constexpr uint32_t InvalidValue=0xA001;
    uint32_t openError=0, readError=0, setError=0, applyError=0, saveError=0;
    float value=.42f;
    bool automatic=true;
    unsigned sets=0, applies=0, saves=0;
    std::vector<float> requested;
    uint32_t open() { return openError; }
    uint32_t read(float& out, bool& automaticOut) { out=value; automaticOut=automatic; return readError; }
    uint32_t set(float in) { ++sets; requested.push_back(in); if(!setError) value=in; return setError; }
    uint32_t apply() { ++applies; return applyError; }
    uint32_t save() { ++saves; return saveError; }
};
int main() {
    Device d; BrightnessControl c(d);
    assert(c.read() && c.available && c.percent==42 && c.automatic);
    assert(d.sets==0 && d.applies==0 && d.saves==0); // opening is read-only
    for (int value : {-100,0,5,50,100,1000}) {
        assert(c.set(value));
        assert(c.percent==static_cast<unsigned>(std::clamp(value,5,100)));
        assert(d.requested.back()>=.05f && d.requested.back()<=1);
        assert(c.automatic && d.automatic && c.unsaved && d.saves==0);
    }
    assert(c.save() && d.saves==1 && !c.unsaved);
    d.value=.73f; d.automatic=false; const auto sets=d.sets;
    assert(c.read() && c.percent==73 && !c.automatic && d.sets==sets); // re-show never restores an old value
    assert(c.set(65) && !d.automatic);
    d.setError=10;
    assert(!c.set(20) && c.result==10 && c.stage==BrightnessStage::Set && c.percent==65);
    assert(!c.save() && d.saves==1); // failed adjustment cannot claim a successful save
    d.setError=0; d.applyError=11;
    assert(!c.set(20) && c.stage==BrightnessStage::Apply && !c.available && c.unsaved);
    assert(!c.save() && d.saves==1);
    d.applyError=0;
    assert(c.set(30) && c.percent==30);
    d.saveError=12;
    assert(!c.save() && c.stage==BrightnessStage::Save && c.unsaved);
    d.saveError=0;
    assert(c.save() && !c.unsaved); // retry a failed save
    for (float invalid : {-1.0f,1.1f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Device broken; broken.value=invalid; BrightnessControl control(broken);
        assert(!control.read() && !control.available && control.result==Device::InvalidValue);
        assert(!control.set(50) && broken.sets==0 && broken.applies==0);
    }
    for (bool opening : {true,false}) {
        Device broken;
        if (opening) broken.openError=20; else broken.readError=21;
        BrightnessControl control(broken);
        assert(!control.set(50) && !control.available && broken.sets==0);
        assert(control.stage==(opening ? BrightnessStage::Open : BrightnessStage::Read));
        broken.openError=broken.readError=0;
        assert(control.read() && control.percent==42 && broken.sets==0);
    }
    d.readError=25;
    assert(!c.set(80) && !c.available && c.stage==BrightnessStage::Read && c.unsaved);
    assert(!c.save()); // failed readback cannot be presented as success
    d.readError=0; assert(c.read() && c.percent==80);
    std::puts("PASS: read-only brightness entry/refresh, clamping, preserved auto brightness, apply/readback failures and explicit save/retry");
}

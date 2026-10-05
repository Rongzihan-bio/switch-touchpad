// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

// Host-only geometry/HID shim. The StepTrackBar implementation itself is read
// directly from the shipped libtesla header by run_brightness_slider_tests.py.
using u8=uint8_t; using u32=uint32_t; using u64=uint64_t;
using s16=int16_t; using s32=int32_t;
constexpr u64 HidNpadButton_AnyLeft=1, HidNpadButton_AnyRight=2;
struct HidTouchState {};
struct HidAnalogStickState {};
namespace tsl::elm {
enum class TouchEvent { Touch, Hold, Scroll, Release };
class TrackBar {
protected:
    s16 m_value=0;
    std::function<void(u8)> m_valueChangedListener=[](u8){};
public:
    explicit TrackBar(const char*) {}
    virtual ~TrackBar()=default;
    virtual bool handleInput(u64,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) { return false; }
    virtual bool onTouch(TouchEvent,s32,s32,s32,s32,s32,s32) { return false; }
    virtual u8 getProgress() { return static_cast<u8>(m_value); }
    virtual void setProgress(u8 value) { m_value=value; }
    void setValueChangedListener(std::function<void(u8)> listener) { m_valueChangedListener=listener; }
    // Track x=0, width=295 => effective slider spans x=60..260, y=0..80.
    int getX() const { return 0; }
    int getWidth() const { return 295; }
    int getTopBound() const { return 0; }
    int getBottomBound() const { return 80; }
    bool inBounds(int x,int y) const { return x>=0 && x<295 && y>=0 && y<80; }
};
}

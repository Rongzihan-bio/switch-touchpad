// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <switch.h>
#include "touch/core.hpp"
#include "common/trace.hpp"
#include "common/layer_stacks.hpp"

namespace touchpad {
struct Runtime {
    Mutex mutex{};
    Engine engine;
    bool ready = false, renderer_ready = false, connected = false;
    uint32_t error = 0, render_error = 0;
    uint64_t sample_heartbeat = 0, render_heartbeat = 0;
    uint32_t input_period_us = 0, render_period_us = 0;
    uint64_t combo = HidNpadButton_L | HidNpadButton_Down | HidNpadButton_StickR;
    LayerHideBarrier layer_hide;
};
extern Runtime runtime;
extern Trace runtimeTrace;
struct Lock {
    Lock() { mutexLock(&runtime.mutex); }
    ~Lock() { mutexUnlock(&runtime.mutex); }
};
uint64_t nowMs();
Status snapshot();
void fail(Result error, Reason reason);
void renderThread(void*);
void serverLoop();
bool saveSettings(const Settings& s);
void readConfiguration();
void refreshCombo();
}

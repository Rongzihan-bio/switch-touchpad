// SPDX-License-Identifier: GPL-2.0-or-later
#include "sysmodule/runtime.hpp"
#include "common/config.hpp"
#include "common/storage.hpp"
#include "common/watchdog.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
// PM's NPDM resource classification does not turn this daemon into an AM applet.
// Do not initialize an applet session or request foreground focus.
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
u32 __nx_nv_transfermem_size = 0x40000;
void __libnx_initheap() {
    // NPDM selects both the Applet physical pool and PM Applet resource group.
    // Keep runtime headroom for the framebuffer, linear canvas, NV transfer
    // memory, stacks and library allocations. This fixed heap is part of BSS.
    alignas(0x1000) static u8 heap[4*1024*1024];
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = reinterpret_cast<char*>(heap);
    fake_heap_end = fake_heap_start + sizeof(heap);
}
void __appInit() {
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);
    if (R_SUCCEEDED(setsysInitialize())) {
        SetSysFirmwareVersion fw{};
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw))) hosversionSet(MAKEHOSVERSION(fw.major,fw.minor,fw.micro));
        setsysExit();
    }
}
void __appExit() { smExit(); }
}

namespace touchpad {
Runtime runtime;
Trace runtimeTrace;
uint64_t nowMs() { return armTicksToNs(armGetSystemTick()) / 1000000; }
Status snapshot() {
    Lock lock;
    Status s = runtime.engine.state();
    s.connected = runtime.connected;
    s.renderer_ready = runtime.renderer_ready;
    s.backend_result = runtime.error ? runtime.error : runtime.render_error;
    if (s.backend_result) s.mode = Mode::Fault;
    s.hos_version = hosversionGet();
    s.input_period_us = runtime.input_period_us;
    s.render_period_us = runtime.render_period_us;
    return s;
}
void fail(Result error, Reason why) {
    Lock lock;
    runtime.engine.stop(why);
    if (why == Reason::Renderer) { runtime.render_error = error; runtime.renderer_ready = false; }
    else runtime.error = error;
}
static std::string readFile(const char* path) {
    std::ifstream in(path);
    std::string out, line;
    while (std::getline(in,line)) {
        if (out.size()+line.size() > 8192) return {};
        out += line + '\n';
    }
    return out;
}
static uint64_t loadCombo() {
    const auto contents = readFile("sdmc:/config/tesla/config.ini");
    bool tesla = false;
    std::istringstream in(contents);
    std::string line;
    while (std::getline(in,line)) {
        line.erase(std::remove_if(line.begin(),line.end(),[](unsigned char c) { return std::isspace(c); }),line.end());
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') { tesla = line == "[tesla]"; continue; }
        if (!tesla) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.find('=',eq+1) != std::string::npos) continue;
        auto key = line.substr(0,eq);
        key.erase(key.find_last_not_of(" \t\r")+1);
        if (key == "key_combo") return parseCombo(line.substr(eq+1), HidNpadButton_L|HidNpadButton_Down|HidNpadButton_StickR);
    }
    return HidNpadButton_L|HidNpadButton_Down|HidNpadButton_StickR;
}
void readConfiguration() {
    Settings s;
    if (loadSettingsFile("sdmc:/config/touchpad/settings.ini",s)) runtime.engine.setSettings(s);
    runtime.engine.stop(Reason::None,true);
    runtime.combo = loadCombo();
}
void refreshCombo() {
    const auto combo = loadCombo();
    Lock lock;
    runtime.combo = combo;
}
bool saveSettings(const Settings& s) {
    if (!valid(s)) return false;
    mkdir("sdmc:/config",0777);
    mkdir("sdmc:/config/touchpad",0777);
    return saveSettingsFile("sdmc:/config/touchpad/settings.ini",s);
}
static uint64_t controllerButtons() {
    uint64_t bits = 0;
    for (unsigned i = 0; i < 9; ++i) {
        const auto id = i == 8 ? HidNpadIdType_Handheld : static_cast<HidNpadIdType>(i);
        HidNpadSystemExtState state{};
        if (hidGetNpadStatesSystemExt(id,&state,1)) bits |= state.buttons;
        const u32 style = hidGetNpadStyleSet(id);
        if (style & HidNpadStyleTag_NpadFullKey) {
            HidNpadFullKeyState p{}; if (hidGetNpadStatesFullKey(id,&p,1)) bits |= p.buttons;
        }
        if (style & HidNpadStyleTag_NpadHandheld) {
            HidNpadHandheldState p{}; if (hidGetNpadStatesHandheld(id,&p,1)) bits |= p.buttons;
        }
        if (style & HidNpadStyleTag_NpadJoyDual) {
            HidNpadJoyDualState p{}; if (hidGetNpadStatesJoyDual(id,&p,1)) bits |= p.buttons;
        }
        if (style & HidNpadStyleTag_NpadJoyLeft) {
            HidNpadJoyLeftState p{}; if (hidGetNpadStatesJoyLeft(id,&p,1)) bits |= p.buttons;
        }
        if (style & HidNpadStyleTag_NpadJoyRight) {
            HidNpadJoyRightState p{}; if (hidGetNpadStatesJoyRight(id,&p,1)) bits |= p.buttons;
        }
    }
    return bits;
}
static void sampler(void*) {
    uint64_t previous = nowMs();
    TimeSteadyClockTimePoint clockPrevious{};
    bool hadClock = false;
    unsigned count = 0;
    bool docked = false;
    for (;;) {
        const uint64_t startNs = armTicksToNs(armGetSystemTick());
        const uint64_t start = startNs/1000000;
        Sample s;
        HidTouchScreenState touch{};
        s.available = hidGetTouchScreenStates(&touch,1) > 0;
        if (s.available) {
            s.counter = touch.sampling_number;
            if (touch.count < 0 || touch.count > 16) s.touches.count = 17;
            else for (int i = 0; i < touch.count; ++i) {
                if (touch.touches[i].attributes & HidTouchAttribute_End) continue;
                s.touches.points[s.touches.count++] = {touch.touches[i].finger_id,static_cast<float>(touch.touches[i].x),static_cast<float>(touch.touches[i].y)};
            }
        }
        HidHomeButtonState home[17]{};
        HidSleepButtonState sleep[17]{};
        // Read samples, not AM's auto-clearing event handles. Preserve system HOME handling.
        static u64 homeCounter = 0, sleepCounter = 0;
        const auto hn = hidGetHomeButtonStates(home,17), sn = hidGetSleepButtonStates(sleep,17);
        for (size_t i = 0; i < hn; ++i) if (home[i].sampling_number > homeCounter && home[i].buttons) s.home = true;
        for (size_t i = 0; i < sn; ++i) if (sleep[i].sampling_number > sleepCounter && sleep[i].buttons) s.sleep = true;
        if (hn) homeCounter = home[0].sampling_number;
        if (sn) sleepCounter = sleep[0].sampling_number;
        uint64_t combo;
        { Lock lock; combo = runtime.combo; }
        s.tesla = (controllerButtons() & combo) == combo;
        u64 pid = 0;
        if (R_SUCCEEDED(pmdmntGetApplicationProcessId(&pid))) s.application_pid = pid;
        if ((count++ % 6) == 0) {
            ApmPerformanceMode mode;
            if (R_SUCCEEDED(apmGetPerformanceMode(&mode))) docked = mode == ApmPerformanceMode_Boost;
            TimeSteadyClockTimePoint point{};
            if (R_SUCCEEDED(timeGetStandardSteadyClockTimePoint(&point))) {
                s.clock_gap = hadClock && (point.time_point < clockPrevious.time_point ||
                    point.time_point - clockPrevious.time_point > 1 ||
                    std::memcmp(&point.source_id,&clockPrevious.source_id,sizeof(Uuid)) != 0);
                clockPrevious = point; hadClock = true;
            } else s.clock_gap = true;
        }
        s.docked = docked;
        uint64_t finished = nowMs();
        s.clock_gap = s.clock_gap || finished < start || finished-start >= TimeoutMs;
        bool stopped=false;
        Status after{};
        {
            Lock lock;
            // Prepare can run while this thread waits for the mutex. Its epoch
            // must not be compared against a timestamp sampled before the lock.
            finished=nowMs();
            const auto before=runtime.engine.state().mode;
            runtime.engine.tick(s,finished);
            after=runtime.engine.state();
            stopped=before!=Mode::Off && before!=Mode::Paused && after.mode==Mode::Paused;
            runtime.input_period_us = static_cast<uint32_t>(std::min<uint64_t>((finished-previous)*1000,UINT32_MAX));
            runtime.sample_heartbeat = finished;
        }
        if (stopped) {
            runtimeTrace.write("sampler-stop-reason",static_cast<u32>(after.reason));
            runtimeTrace.write("sampler-stop-period-ms",finished>=previous ? finished-previous : 0);
            runtimeTrace.write("sampler-stop-touch-age-ms",after.sample_age_ms);
        }
        previous = finished;
        const auto usedNs = armTicksToNs(armGetSystemTick()) - startNs;
        if (usedNs < 16666667) svcSleepThread(static_cast<s64>(16666667-usedNs));
    }
}
static void injector(void* arg) {
    const auto session = *static_cast<HiddbgHdlsSessionId*>(arg);
    HiddbgHdlsHandle handle{};
    TimeSteadyClockTimePoint previousClock{};
    bool hadClock = false;
    Result previousError=0;
    for (;;) {
        TimeSteadyClockTimePoint clock{};
        const Result clockResult = timeGetStandardSteadyClockTimePoint(&clock);
        const bool clockGap = R_FAILED(clockResult) || (hadClock &&
            (clock.time_point < previousClock.time_point || clock.time_point-previousClock.time_point > 1 ||
             std::memcmp(&clock.source_id,&previousClock.source_id,sizeof(Uuid)) != 0));
        if (R_SUCCEEDED(clockResult)) { previousClock = clock; hadClock = true; }
        Status s;
        WatchdogCheck watchdog;
        {
            Lock lock;
            const auto mode = runtime.engine.state().mode;
            watchdog=checkWatchdogs(mode,clockGap,runtime.sample_heartbeat,runtime.render_heartbeat,nowMs);
            if (watchdog.reason!=Reason::None) runtime.engine.stop(watchdog.reason);
            s = runtime.engine.state();
        }
        const bool wantsDevice = s.mode == Mode::Prepared || s.mode == Mode::PairPrepared || s.mode == Mode::WaitingTouch ||
                                 s.mode == Mode::Active || s.mode == Mode::Pairing;
        Result rc = 0;
        if (wantsDevice && !handle.handle) {
            HiddbgHdlsDeviceInfo info{};
            info.deviceType = HidDeviceType_FullKey3;
            info.npadInterfaceType = HidNpadInterfaceType_USB;
            info.singleColorBody = 0x243944; info.singleColorButtons = 0x2ACCB4;
            info.colorLeftGrip = info.colorRightGrip = 0x243944;
            runtimeTrace.write("hdls-attach-begin");
            rc = hiddbgAttachHdlsVirtualDevice(&handle,&info);
            runtimeTrace.write("hdls-attach-end",handle.handle,rc);
        }
        if (R_SUCCEEDED(rc) && handle.handle) {
            HiddbgHdlsState state{};
            state.battery_level = 4;
            // Refresh after potentially blocking attachment; don't replay a pre-stop snapshot.
            s = snapshot();
            if ((s.mode == Mode::Active || s.mode == Mode::Pairing) && valid(s.pad,s.mode == Mode::Pairing)) {
                state.buttons = s.pad.buttons;
                state.analog_stick_l = {s.pad.lx,s.pad.ly};
            }
            rc = hiddbgSetHdlsState(handle,&state);
            if (s.mode == Mode::Off || s.mode == Mode::Fault || R_FAILED(rc)) {
                HiddbgHdlsState neutral{}; neutral.battery_level = 4;
                hiddbgSetHdlsState(handle,&neutral);
                const Result detach = hiddbgDetachHdlsVirtualDevice(handle);
                if (R_SUCCEEDED(rc)) rc = detach;
                if (R_SUCCEEDED(detach)) handle = {};
                else {
                    bool attached = true;
                    if (R_SUCCEEDED(hiddbgIsHdlsVirtualDeviceAttached(session,handle,&attached)) && !attached) handle = {};
                    // Retain an uncertain handle so subsequent cycles keep trying to neutralize it.
                }
            }
        }
        {
            Lock lock;
            runtime.connected = handle.handle != 0;
            if (R_FAILED(rc)) { runtime.engine.stop(Reason::Backend); runtime.error = rc; }
        }
        // Log only after the neutral state has been sent; no SD writes while
        // holding runtime.mutex or before releasing an active virtual button.
        if (watchdog.reason!=Reason::None) {
            runtimeTrace.write("watchdog-stop-reason",static_cast<u32>(watchdog.reason),clockResult);
            runtimeTrace.write("watchdog-sample-age-ms",watchdog.sample_age);
            runtimeTrace.write("watchdog-render-age-ms",watchdog.render_age);
        }
        if (R_FAILED(rc) && rc!=previousError) runtimeTrace.write("injector-error",0,rc);
        previousError=rc;
        svcSleepThread(8000000);
    }
}
}

int main() {
    using namespace touchpad;
    Result rc = fsInitialize();
    if (R_SUCCEEDED(rc)) rc = fsdevMountSdmc();
    FILE* startupLog = nullptr;
    if (R_SUCCEEDED(rc)) {
        mkdir("sdmc:/config",0777);
        mkdir("sdmc:/config/touchpad",0777);
        startupLog = std::fopen("sdmc:/config/touchpad/startup.log","w");
        runtimeTrace.open("sdmc:/config/touchpad/runtime.log","sdmc:/config/touchpad/runtime.previous.log");
        runtimeTrace.write("1.1.0-beta.2-backend-start");
    }
    auto record = [&](const char* stage) {
        if (!startupLog) return;
        std::fprintf(startupLog,"%s: %08X\n",stage,rc);
        std::fflush(startupLog);
        fsync(fileno(startupLog));
    };
    record("1.1.0-beta.2 mount SD");
    record("configured pool=Applet group=Applet heap=4194304 stack=65536");
    if (R_SUCCEEDED(rc)) readConfiguration();
    if (R_SUCCEEDED(rc)) rc = hidInitialize();
    record("hidInitialize");
    if (R_SUCCEEDED(rc)) rc = hidsysInitialize();
    record("hidsysInitialize");
    if (R_SUCCEEDED(rc)) rc = pmdmntInitialize();
    record("pmdmntInitialize");
    if (R_SUCCEEDED(rc)) rc = timeInitialize();
    record("timeInitialize");
    if (R_SUCCEEDED(rc)) rc = apmInitialize();
    record("apmInitialize");
    if (R_SUCCEEDED(rc)) rc = hiddbgInitialize();
    record("initialize services");
    alignas(0x1000) static u8 hdlsWork[0x1000];
    HiddbgHdlsSessionId session{};
    if (R_SUCCEEDED(rc)) rc = hiddbgAttachHdlsWorkBuffer(&session,hdlsWork,sizeof(hdlsWork));
    record("attach HDLS");
    if (R_SUCCEEDED(rc)) {
        hidInitializeTouchScreen();
        padConfigureInput(8,HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadSystemExt);
        const struct { u8 enabled; u8 padding[7]; u64 aruid; } in{1,{},0};
        rc = serviceDispatchIn(hidsysGetServiceSession(),503,in);
        if (R_SUCCEEDED(rc)) rc = hidsysActivateHomeButton();
        if (R_SUCCEEDED(rc)) rc = hidsysActivateSleepButton();
    }
    record("configure HID");
    Thread sensor{}, sender{}, renderer{};
    if (R_SUCCEEDED(rc)) rc = threadCreate(&sensor,sampler,nullptr,nullptr,0x6000,0x2c,3);
    if (R_SUCCEEDED(rc)) rc = threadStart(&sensor);
    if (R_SUCCEEDED(rc)) rc = threadCreate(&sender,injector,&session,nullptr,0x6000,0x2b,3);
    if (R_SUCCEEDED(rc)) rc = threadStart(&sender);
    if (R_SUCCEEDED(rc)) rc = threadCreate(&renderer,renderThread,nullptr,nullptr,0x8000,0x32,3);
    if (R_SUCCEEDED(rc)) rc = threadStart(&renderer);
    record("start threads");
    if (startupLog) std::fclose(startupLog);
    { Lock lock; runtime.ready = R_SUCCEEDED(rc); runtime.error = rc; }
    serverLoop();
    // The registered background service is intended to live until system shutdown.
    // If its IPC listener fails, leave threads running in the disabled state.
    fail(MAKERESULT(Module_Libnx,LibnxError_BadInput),Reason::Backend);
    for (;;) svcSleepThread(1000000000);
}

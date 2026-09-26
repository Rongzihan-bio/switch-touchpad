// SPDX-License-Identifier: GPL-2.0-or-later
#include <switch.h>
#include "common/client.hpp"
#include <cstdio>

int main() {
    consoleInit(nullptr);
    // Application's session quota is normally 1. Release the default SM named
    // port after initialization so the test can open the Touchpad named port.
    // Display/HID services are already initialized; no new SM services are used.
    smExit();
    padConfigureInput(8,HidNpadStyleSet_NpadStandard);
    PadState exitPad;
    padInitializeAny(&exitPad);
    touchpad::Client client;
    touchpad::Status status;
    Result rc = 0;
    u64 last = 0;
    while (appletMainLoop()) {
        padUpdate(&exitPad);
        if (padGetButtonsDown(&exitPad) & HidNpadButton_Plus) break;
        const u64 now = armTicksToNs(armGetSystemTick())/1000000;
        if (now-last >= 100) {
            last = now;
            rc = client.call(touchpad::Request{},status);
            std::printf("\x1b[2J\x1b[HTouchpad Input Test 0.1.1-rc10   (+ exits)\n\n");
            const auto mode=appletGetAppletType();
            const bool app=mode==AppletType_Application || mode==AppletType_SystemApplication;
            std::printf("Mode: %s\n",app ? "APPLICATION" : "APPLET - reopen hbmenu holding R while starting a game");
            std::printf("Tesla > Touchpad > Enable and return to game; lift all fingers.\n");
            std::printf("Top: state RECEIVED by this application. Bottom: backend diagnostics.\n\n");
            for (unsigned i = 0; i < 9; ++i) {
                const auto id = i == 8 ? HidNpadIdType_Handheld : static_cast<HidNpadIdType>(i);
                const auto style = hidGetNpadStyleSet(id);
                HidNpadCommonState state{};
                size_t got = 0;
                if (style & HidNpadStyleTag_NpadFullKey) got = hidGetNpadStatesFullKey(id,&state,1);
                else if (style & HidNpadStyleTag_NpadHandheld) got = hidGetNpadStatesHandheld(id,&state,1);
                else if (style & HidNpadStyleTag_NpadJoyDual) got = hidGetNpadStatesJoyDual(id,&state,1);
                std::printf("%s%u style %08X  %s  A:%u B:%u X:%u Y:%u\n",i == 8 ? "HH" : "P",i == 8 ? 0 : i+1,style,got ? "present" : "absent ",
                    !!(state.buttons&HidNpadButton_A),!!(state.buttons&HidNpadButton_B),!!(state.buttons&HidNpadButton_X),!!(state.buttons&HidNpadButton_Y));
                std::printf("   left (%6d,%6d) buttons %016llX\n",state.analog_stick_l.x,state.analog_stick_l.y,static_cast<unsigned long long>(state.buttons));
            }
            std::printf("\nBackend IPC: %08X   mode: %s   reason: %s\n",rc,touchpad::modeName(status.mode),touchpad::reasonName(status.reason));
            std::printf("Backend result: %08X   HOS: %u.%u.%u\n",status.backend_result,(status.hos_version>>16)&255,(status.hos_version>>8)&255,status.hos_version&255);
            std::printf("Connected: %u  display: %u  touches: %u  sample age: %ums\n",status.connected,status.renderer_ready,status.touch_count,status.sample_age_ms);
            std::printf("Input period: %uus  render work: %uus  sequence: %llu\n",status.input_period_us,status.render_period_us,static_cast<unsigned long long>(status.sequence));
            std::printf("\nABXY alone cannot operate games requiring shoulder, +/- or right stick.\n");
        }
        consoleUpdate(nullptr);
    }
    touchpad::Request stop; stop.command = touchpad::Command::Pause;
    client.call(stop,status); client.close();
    consoleExit(nullptr);
    return 0;
}

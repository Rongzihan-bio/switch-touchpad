// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/watchdog.hpp"
#include "touch/core.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <utility>

int main() {
    using namespace touchpad;
    uint64_t now=1000, sample=1000, render=1000;
    auto clock=[&] { return now; };
    const auto staleTime=now; // injector's old pre-IPC timestamp
    now+=8;
    sample=render=now; // producers run during time-service IPC
    assert(staleTime<sample && staleTime<render); // rc8 incorrectly stops here
    auto check=checkWatchdogs(Mode::Active,false,sample,render,clock);
    assert(check.reason==Reason::None && check.now==1008);

    // Rendering may publish after the sample thread; either heartbeat must be
    // compared with a clock captured inside the same locked observation.
    now=1020; render=1020;
    assert(checkWatchdogs(Mode::Active,false,sample,render,clock).reason==Reason::None);
    now=sample+249;
    assert(checkWatchdogs(Mode::Active,false,sample,render,clock).reason==Reason::None);
    ++now;
    assert(checkWatchdogs(Mode::Active,false,sample,render,clock).reason==Reason::Timeout);
    sample=now; render=now-250;
    check=checkWatchdogs(Mode::Active,false,sample,render,clock);
    assert(check.reason==Reason::None && check.render_age==250);
    assert(checkWatchdogs(Mode::Prepared,false,sample,render,clock).reason==Reason::None);
    assert(checkWatchdogs(Mode::Active,true,0,0,clock).reason==Reason::ClockGap);
    assert(checkWatchdogs(Mode::Paused,true,0,0,clock).reason==Reason::None);

    Engine engine;
    now=10000;
    assert(engine.prepare(false,42,now)==Error::Ok);
    assert(engine.commit(engine.state().token,now)==Error::Ok);
    Sample input;
    input.available=true; input.application_pid=42;
    unsigned oldFalsePositives=0;
    for (unsigned frame=0; frame<7500; ++frame) { // 120 simulated seconds
        const auto beforeIpc=now;
        now+=16;
        input.counter++;
        if (frame) {
            input.touches.count=1;
            input.touches.points[0]={7,1175,540}; // hold A
        }
        engine.tick(input,now);
        sample=render=now;
        if (beforeIpc<sample) ++oldFalsePositives;
        const auto result=checkWatchdogs(engine.state().mode,false,sample,render,clock);
        if (result.reason!=Reason::None) engine.stop(result.reason);
        assert(engine.state().mode==Mode::Active);
        if (frame) assert(engine.state().pad.buttons==ButtonA);
    }
    assert(oldFalsePositives==7500);
    // Replay the measured rc9 failures: touch remains current while drawing
    // reaches 254/250/256 ms. These must no longer pause the controller.
    for (const auto& ages : {std::pair<uint64_t,uint64_t>{47,254}, {70,250}, {117,256}}) {
        check=checkWatchdogs(Mode::Active,false,now-ages.first,now-ages.second,clock);
        assert(check.reason==Reason::None);
    }
    // Freeze the display heartbeat for 24 simulated seconds while samples keep
    // arriving. Both presses and releases must reach the live engine throughout.
    render=now;
    input.touches.points[1]={8,260,540}; // move right while pressing A
    for (unsigned frame=0; frame<1500; ++frame) {
        now+=16; ++input.counter;
        input.touches.count=((frame/17)%2)*2;
        engine.tick(input,now); sample=now;
        check=checkWatchdogs(engine.state().mode,false,sample,render,clock);
        if (check.reason!=Reason::None) engine.stop(check.reason);
        assert(engine.state().mode==Mode::Active);
        assert(engine.state().pad.buttons==(input.touches.count ? ButtonA : 0));
        assert(input.touches.count ? engine.state().pad.lx>0 : engine.state().pad.lx==0);
        assert(engine.state().pad.ly==0);
    }
    render=now; // display recovery does not need a new Prepare/Commit
    assert(checkWatchdogs(engine.state().mode,false,sample,render,clock).reason==Reason::None);
    now+=250;
    check=checkWatchdogs(engine.state().mode,false,sample,render,clock);
    assert(check.reason==Reason::Timeout);
    engine.stop(check.reason);
    assert(engine.state().mode==Mode::Paused && engine.state().pad.buttons==0);
    sample=render=now;
    engine.tick(input,now); // a fresh sample does not silently re-enable
    assert(engine.state().mode==Mode::Paused);
    // A real display failure (as opposed to an old heartbeat) still releases.
    assert(engine.prepare(false,42,now)==Error::Ok);
    assert(engine.commit(engine.state().token,now)==Error::Ok);
    input.touches.count=0; ++input.counter; now+=16; engine.tick(input,now);
    input.touches.count=1; ++input.counter; now+=16; engine.tick(input,now);
    assert(engine.state().pad.buttons==ButtonA);
    engine.stop(Reason::Renderer);
    assert(engine.state().mode==Mode::Paused && engine.state().pad.buttons==0);
    std::puts("PASS: timestamp race, measured load stalls, 24s frozen display with live presses/releases, real sample timeout and display-error release");
}

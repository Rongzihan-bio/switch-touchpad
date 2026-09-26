// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/protocol.hpp"

namespace touchpad {
struct WatchdogCheck {
    Reason reason=Reason::None;
    uint64_t now=0, sample_age=0, render_age=0;
};
// The caller holds the runtime mutex. Read the clock *after* taking the
// heartbeat snapshot, never before an IPC which lets another thread publish.
template<class Clock>
WatchdogCheck checkWatchdogs(Mode mode, bool clockGap, uint64_t sampleHeartbeat,
                            uint64_t renderHeartbeat, Clock readNow) {
    WatchdogCheck check;
    check.now=readNow();
    check.sample_age=check.now>=sampleHeartbeat ? check.now-sampleHeartbeat : 0;
    check.render_age=check.now>=renderHeartbeat ? check.now-renderHeartbeat : 0;
    if (mode==Mode::Off || mode==Mode::Paused) return check;
    if (clockGap) check.reason=Reason::ClockGap;
    else if (check.now<sampleHeartbeat || check.sample_age>=TimeoutMs) check.reason=Reason::Timeout;
    // Drawing is only feedback, not the source of input. A busy compositor must
    // not stop healthy touch sampling. Actual renderer errors still call fail().
    // Keep render_age for diagnostics, but never turn its age into a stop.
    return check;
}
}

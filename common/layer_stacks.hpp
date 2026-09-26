// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace touchpad {
// Tracks only successful VI stack changes. A transparent buffer is still a
// submitted layer; an inactive overlay must leave the display stacks entirely.
class LayerStacks {
    uint32_t target_, members_ = 0;
public:
    explicit LayerStacks(uint32_t target) : target_(target) {}
    bool empty() const { return members_ == 0; }
    template<class Change> uint32_t hide(Change change) {
        uint32_t first = 0;
        for (unsigned i=0; i<32; ++i) if (members_ & (1U<<i)) {
            const auto rc=change(i,false);
            if (!rc) members_ &= ~(1U<<i);
            else if (!first) first=rc;
        }
        return first;
    }
    template<class Change> uint32_t show(Change change) {
        for (unsigned i=0; i<32; ++i) if ((target_ & (1U<<i)) && !(members_ & (1U<<i))) {
            const auto rc=change(i,true);
            if (rc) { hide(change); return rc; }
            members_ |= 1U<<i;
        }
        return 0;
    }
};
// A renderer acknowledges the generation it observed *before* removing its
// layer. Never acknowledge a later request while an older frame can still show.
struct LayerHideBarrier {
    uint64_t requested=0, completed=0;
    uint64_t request() { return ++requested; }
    void acknowledge(uint64_t observed) { if (observed>completed) completed=observed; }
    bool done(uint64_t ticket) const { return completed>=ticket; }
};
}

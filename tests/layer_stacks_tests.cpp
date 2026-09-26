// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/layer_stacks.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <set>

int main() {
    using namespace touchpad;
    LayerStacks game(3), menu(0x47F);
    std::map<unsigned,std::set<unsigned>> screen;
    unsigned calls=0;
    auto change=[&](unsigned owner,unsigned stack,bool add) -> uint32_t {
        ++calls;
        if (add) {
            // Model a compositor that cannot keep two custom layers in a stack.
            if (!screen[stack].empty() && !screen[stack].count(owner)) return 99;
            assert(screen[stack].insert(owner).second);
        } else assert(screen[stack].erase(owner)==1);
        return 0;
    };
    auto gameChange=[&](unsigned s,bool a) {return change(1,s,a);};
    auto menuChange=[&](unsigned s,bool a) {return change(2,s,a);};
    assert(game.hide(gameChange)==0 && calls==0); // startup is truly absent
    assert(menu.show(menuChange)==0);
    assert(game.show(gameChange)==99 && game.empty()); // coexistence would fail
    assert(menu.hide(menuChange)==0 && menu.empty());
    assert(game.show(gameChange)==0);
    const auto previousCalls=calls;
    assert(game.show(gameChange)==0 && calls==previousCalls); // no repeated adds
    LayerHideBarrier barrier;
    const auto request=barrier.request();
    barrier.acknowledge(0); // a frame from before Pause cannot acknowledge it
    assert(!barrier.done(request));
    assert(game.hide(gameChange)==0);
    barrier.acknowledge(request);
    assert(barrier.done(request) && menu.show(menuChange)==0);
    assert(menu.hide(menuChange)==0);
    for (const auto& [id,owners]:screen) { (void)id; assert(owners.empty()); }

    LayerStacks partial(3);
    bool failRemoval=true;
    auto failure=[&](unsigned stack,bool add) -> uint32_t {
        if (add && stack==1) return 12; // partial attach rolls back first stack
        if (!add && failRemoval) return 13;
        return 0;
    };
    assert(partial.show(failure)==12 && !partial.empty()); // failed rollback retained
    failRemoval=false;
    assert(partial.hide(failure)==0 && partial.empty()); // recoverable on retry
    const auto later=barrier.request();
    barrier.acknowledge(request);
    assert(!barrier.done(later));
    std::puts("PASS: inactive layers absent, exclusive menu/game handoff, partial failure recovery and late-frame hide barrier");
}

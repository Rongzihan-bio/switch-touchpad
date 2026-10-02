// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch/core.hpp"
#include "touch/draw.hpp"
#include "common/edge_swipe.hpp"
#include "tester/exit_chord.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace touchpad;
static void shoulders() {
    // HID assignments, including forbidden stick clicks, are part of the ABI.
    assert(ButtonL == 0x40 && ButtonR == 0x80 && ButtonZL == 0x100 && ButtonZR == 0x200);
    assert(!valid(PadState{1ULL<<4}) && !valid(PadState{1ULL<<5}));
    assert(!valid(PadState{ButtonZL},true) && !valid(PadState{ButtonZR},true));
    for (auto mode : {StickMode::Analog,StickMode::Arcade8}) {
        for (unsigned scale : {75U,100U,125U}) {
            Settings s; s.scale=scale; s.stick_mode=mode;
            const auto l=Layout::make(s);
            Mapper mapper;
            for (size_t i=0;i<l.buttons.size();++i) {
                const auto c=l.buttons[i];
                assert(c.x-c.radius>16 && c.x+c.radius<1264 && c.y-c.radius>0 && c.y+c.radius<720);
                assert(std::hypot(c.x-l.stick.x,c.y-l.stick.y)>c.radius+l.stick.radius);
                for (size_t j=0;j<i;++j)
                    assert(std::hypot(c.x-l.buttons[j].x,c.y-l.buttons[j].y)>c.radius+l.buttons[j].radius);
                // Slide the same finger between all buttons; no old button sticks.
                Touches t; t.count=1; t.points[0]={1,c.x,c.y};
                assert(mapper.update(t,s).buttons==Layout::ButtonBits[i]);
            }
            assert(mapper.update({},s).buttons==0);
            Touches t; t.count=static_cast<uint32_t>(l.buttons.size()+1);
            t.points[0]={0,l.stick.x+l.stick.radius,l.stick.y};
            for (unsigned i=0;i<l.buttons.size();++i) t.points[i+1]={i+1,l.buttons[i].x,l.buttons[i].y};
            auto p=mapper.update(t,s);
            assert((p.buttons&TouchButtons)==TouchButtons && valid(p));
            assert(mode==StickMode::Analog ? p.lx==32767 : !!(p.buttons&ButtonRight));
            t.count=5; p=mapper.update(t,s);
            assert((p.buttons&TouchButtons)==(ButtonA|ButtonB|ButtonX|ButtonY));
            // A captured stick owner dragged over a shoulder still only moves the stick.
            t.count=1; t.points[0]={0,l.buttons[4].x,l.buttons[4].y};
            assert((mapper.update(t,s).buttons&TouchButtons)==0);
            mapper.reset();
            t.count=2; t.points[0]={9,l.buttons[5].x,l.buttons[5].y}; t.points[1]=t.points[0]; t.points[1].id=10;
            assert(mapper.update(t,s).buttons==ButtonZL);
            t.count=1;
            assert(mapper.update(t,s).buttons==ButtonZL);
            assert(mapper.update({},s).buttons==0);
        }
    }
    Engine e; Sample sample; sample.available=true; sample.application_pid=1;
    assert(e.prepare(false,1,0)==Error::Ok);
    assert(e.commit(e.state().token,0)==Error::Ok);
    sample.counter=1; e.tick(sample,16);
    sample.touches.count=4;
    const auto l=Layout::make({});
    for (unsigned i=0;i<4;++i) sample.touches.points[i]={i,l.buttons[i+4].x,l.buttons[i+4].y};
    ++sample.counter; e.tick(sample,32);
    assert(e.state().pad.buttons==(ButtonL|ButtonZL|ButtonR|ButtonZR));
    sample.sleep=true; ++sample.counter; e.tick(sample,48);
    assert(e.state().mode==Mode::Paused && e.state().pad.buttons==0);
}
static void menuButtons() {
    assert(ButtonPlus==0x400 && ButtonMinus==0x800);
    assert(!valid(PadState{ButtonPlus},true) && !valid(PadState{ButtonMinus},true));
    assert(!valid(PadState{1ULL<<18}) && !valid(PadState{1ULL<<19})); // no HOME/Capture
    const auto l=Layout::make({});
    for (unsigned index : {8U,9U}) {
        Mapper mapper; Touches t; t.count=2;
        t.points[0]={1,l.buttons[index].x,l.buttons[index].y};
        t.points[1]=t.points[0]; t.points[1].id=2;
        assert(mapper.update(t,{}).buttons==Layout::ButtonBits[index]);
        t.count=1; assert(mapper.update(t,{}).buttons==Layout::ButtonBits[index]);
        t.points[0].x+=l.buttons[index].radius+1;
        assert(mapper.update(t,{}).buttons==0);
    }
    for (unsigned event=0;event<5;++event) {
        Engine e; Sample sample; sample.available=true; sample.application_pid=1;
        assert(e.prepare(false,1,0)==Error::Ok);
        assert(e.commit(e.state().token,0)==Error::Ok);
        sample.counter=1; e.tick(sample,16);
        sample.touches.count=2;
        sample.touches.points[0]={1,l.buttons[8].x,l.buttons[8].y};
        sample.touches.points[1]={2,l.buttons[9].x,l.buttons[9].y};
        ++sample.counter; e.tick(sample,32);
        assert(e.state().mode==Mode::Active && e.state().pad.buttons==(ButtonMinus|ButtonPlus));
        switch (event) {
            case 0: sample.home=true; break;
            case 1: sample.sleep=true; break;
            case 2: sample.tesla=true; break;
            case 3: sample.application_pid=2; break;
            case 4: e.stop(Reason::User); break;
        }
        ++sample.counter; e.tick(sample,48);
        assert(e.state().mode==Mode::Paused && e.state().pad.buttons==0);
    }
    TestExitChord exit;
    constexpr auto both=ButtonPlus|ButtonMinus;
    for (uint64_t now=0;now<2500;now+=20) assert(!exit.update(ButtonPlus,now));
    for (uint64_t now=2500;now<5000;now+=20) assert(!exit.update(ButtonMinus,now));
    for (uint64_t now=5000;now<6500;now+=20) assert(!exit.update(both,now));
    assert(!exit.update(0,6500)); // a brief release must restart the hold
    for (uint64_t now=6520;now<8520;now+=20) assert(!exit.update(both,now));
    assert(exit.update(both,8520));
    assert(!exit.update(0,8540));
    assert(!exit.update(both,8560)); assert(!exit.update(both,11000)); // no elapsed sleep time
    assert(!exit.update(both,100)); // clock reset
    for (uint64_t now=120;now<2100;now+=20) assert(!exit.update(both,now));
    assert(exit.update(both,2100));
}
static void swipes() {
    SwipeSettings s{true,false,0}; EdgeSwipe swipe;
    auto update=[&](unsigned count,int x,uint64_t ms,unsigned id=7,bool available=true,bool hidden=true) {
        return swipe.update(s,available,hidden,count,id,x,350,ms);
    };
    // An already-held finger when the overlay starts cannot invoke it.
    assert(!update(1,1,1000)); assert(!update(1,120,1020));
    assert(!update(0,0,1040)); assert(!update(1,16,1060));
    assert(!update(1,100,1080)); assert(update(1,101,1100));
    assert(!update(1,120,1120)); // one event per gesture
    assert(!update(0,0,1140));
    assert(!update(1,17,1160)); assert(!update(1,0,1180)); assert(!update(1,120,1200)); // not edge-started
    assert(!update(0,0,1220)); assert(!update(1,0,1240));
    assert(!update(2,20,1260)); assert(!update(1,120,1280)); // no multi-touch takeover
    assert(!update(0,0,1300)); assert(!update(1,0,1320)); assert(!update(1,120,1340,8));
    assert(!update(0,0,1360)); assert(!update(1,0,1380)); assert(!update(1,120,1531)); // >150 ms
    assert(!update(0,0,1550)); assert(!update(1,0,1570)); assert(update(1,120,1720)); // 150 ms boundary
    assert(!update(0,0,1740)); assert(!update(1,0,1760)); swipe.cancel();
    assert(!update(1,120,1780)); // HOME/sleep cancels old gesture
    assert(!update(0,0,1800)); assert(!update(1,0,1820)); assert(!update(1,120,10000)); // resume gap
    assert(!update(0,0,10020)); assert(!update(1,0,10040)); assert(!update(1,120,9999)); // clock backwards
    assert(!update(0,0,10060)); assert(!update(1,0,10080)); assert(!update(1,40,10100,7,false));
    assert(!update(1,120,10120)); // missing HID data cannot finish old swipe
    assert(!update(0,0,10140)); assert(!update(1,0,10160)); assert(!update(1,120,10180,7,true,false));
    assert(!update(1,120,10200)); // an open menu never responds to swipe
    s.enabled=false;
    assert(!update(0,0,10220)); assert(!update(1,0,10240)); assert(!update(1,120,10260));
    s.enabled=true; s.right=true;
    assert(!update(0,0,10280)); assert(!update(1,1264,10300)); assert(!update(1,1180,10320)); assert(update(1,1179,10340));
    s.right=false; s.offset=12;
    assert(!update(0,0,10360)); assert(!update(1,28,10380)); assert(!update(1,112,10400)); assert(update(1,113,10420));
    assert(!update(0,0,10440)); assert(!update(1,-1,10460)); assert(!update(1,200,10480));
}
static void drawing() {
    Settings s;
    for (unsigned scale : {75U,100U,125U}) for (unsigned divisor : {1U,2U}) {
        s.scale=scale;
        const unsigned w=1280/divisor,h=720/divisor;
        std::vector<uint16_t> pixels(w*h+2,0xBEEF), idle;
        draw(pixels.data()+1,w,s,{},true,divisor); idle=pixels;
        for (unsigned i=4;i<Layout::ButtonBits.size();++i) {
            draw(pixels.data()+1,w,s,PadState{Layout::ButtonBits[i]},true,divisor);
            assert(pixels.front()==0xBEEF && pixels.back()==0xBEEF);
            assert(pixels!=idle && pixels[1+(h/2)*w+w/2]==0);
            const auto c=Layout::make(s).buttons[i];
            const unsigned x=static_cast<unsigned>(c.x/divisor),y=static_cast<unsigned>((c.y+c.radius*.7f)/divisor);
            const auto color=pixels[1+y*w+x];
            assert((color&15)==2 && ((color>>4)&15)==12);
        }
    }
    // Inspect actual half-resolution framebuffer, upscaled by the compositor.
    s.scale=100; s.stick_mode=StickMode::Arcade8;
    std::vector<uint16_t> pixels(640*360);
    draw(pixels.data(),640,s,PadState{ButtonL|ButtonZR|ButtonA|ButtonDown|ButtonRight|ButtonPlus},true,2);
    FILE* f=std::fopen("build/host/controls-preview.ppm","wb"); assert(f);
    std::fprintf(f,"P6\n1280 720\n255\n");
    for (unsigned y=0;y<720;++y) for (unsigned x=0;x<1280;++x) {
        const auto c=pixels[(y/2)*640+x/2]; const unsigned a=c>>12;
        unsigned char rgb[3];
        for (unsigned k=0;k<3;++k) rgb[k]=(((c>>(k*4))&15)*17*a+25*(15-a))/15;
        assert(std::fwrite(rgb,1,3,f)==3);
    }
    std::fclose(f);
}
int main() {
    shoulders(); menuButtons(); swipes(); drawing();
    std::puts("PASS: shoulder/plus/minus HID mapping, combinations, release, layout, rendering, test exit chord and edge-swipe lifecycle");
}

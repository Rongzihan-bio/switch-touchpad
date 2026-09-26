// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch/arcade.hpp"
#include "touch/core.hpp"
#include "touch/draw.hpp"
#include "common/config.hpp"
#include "common/storage.hpp"
#include "common/watchdog.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <vector>

using namespace touchpad;
static Touches fingers(std::initializer_list<Point> points) {
    Touches t; t.count=points.size(); unsigned i=0;
    for (auto p:points) t.points[i++]=p;
    return t;
}
static bool neutral(const PadState& p) { return !p.buttons && !p.lx && !p.ly && !p.rx && !p.ry; }
static Point position(unsigned direction, const Settings& s, uint32_t id=1) {
    const auto c=Layout::make(s).stick;
    const auto axes=directionAxes(static_cast<Direction>(direction));
    return {id,c.x+axes.x*c.radius*0.6f,c.y-axes.y*c.radius*0.6f};
}
static void sectors() {
    assert(arcadeDirection(0,0,0)==Direction::Neutral);
    assert(arcadeDirection(0.2f,0,0.2f)==Direction::Neutral);
    assert(arcadeDirection(0.2001f,0,0.2f)==Direction::Right);
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    assert(arcadeDirection(nan,0,0.2f)==Direction::Neutral);
    assert(arcadeDirection(1,0,nan)==Direction::Neutral);
    assert(arcadeDirection(1,0,-1)==Direction::Neutral);
    constexpr Direction ordered[]={Direction::Right,Direction::UpRight,Direction::Up,Direction::UpLeft,
        Direction::Left,Direction::DownLeft,Direction::Down,Direction::DownRight};
    constexpr double pi=3.14159265358979323846;
    for (unsigned i=0;i<8;++i) {
        for (double offset:{-22.4,0.0,22.4}) {
            const double a=(45*i+offset)*pi/180;
            assert(arcadeDirection(std::cos(a),std::sin(a),0.2f)==ordered[i]);
        }
        const double before=(45*i+22.49)*pi/180, after=(45*i+22.51)*pi/180;
        for (unsigned repeat=0;repeat<5;++repeat) {
            assert(arcadeDirection(std::cos(before),std::sin(before),0.2f)==ordered[i]);
            assert(arcadeDirection(std::cos(after),std::sin(after),0.2f)==ordered[(i+1)%8]);
        }
    }
    for (unsigned i=1;i<=9;++i) {
        const auto d=static_cast<Direction>(i);
        for (auto output:{ArcadeOutput::Dpad,ArcadeOutput::LeftStick}) {
            PadState pad{ButtonA}; applyArcadeDirection(pad,d,output);
            assert(valid(pad) && (pad.buttons & ButtonA));
            assert(padDirection(pad)==d);
            if (output==ArcadeOutput::Dpad) assert(!pad.lx && !pad.ly);
            else assert(!(pad.buttons & DirectionButtons) && std::hypot(pad.lx,pad.ly)<=32768);
            applyArcadeDirection(pad,Direction::Neutral,output);
            assert(pad.buttons==ButtonA && !pad.lx && !pad.ly);
        }
    }
    assert(!valid(PadState{ButtonLeft|ButtonRight}));
    assert(!valid(PadState{ButtonUp|ButtonDown}));
    assert(!valid(PadState{ButtonDown|ButtonLeft},true));
}
static void gestures() {
    Settings s; s.stick_mode=StickMode::Arcade8;
    for (auto output:{ArcadeOutput::Dpad,ArcadeOutput::LeftStick}) {
        s.arcade_output=output;
        for (uint32_t scale:{75U,100U,125U}) {
            s.scale=scale;
            const auto layout=Layout::make(s);
            const auto c=layout.stick;
            Mapper m;
            // Quarter circles in both directions, dragon punch, half circle,
            // two taps with neutral, and immediate direction reversal.
            for (unsigned d:{5,2,3,6,5,2,1,4,5,6,2,3,5,4,1,2,3,6,5,6,5,6,4,6,5}) {
                auto pad=m.update(fingers({position(d,s)}),s);
                assert(padDirection(pad)==static_cast<Direction>(d));
                assert(valid(pad));
            }
            auto pad=m.update(fingers({{1,c.x+4*c.radius,c.y}}),s);
            assert(padDirection(pad)==Direction::Right); // outside the outer circle stays active
            pad=m.update(fingers({{1,c.x,c.y}}),s);
            assert(neutral(pad));
            pad=m.update(fingers({position(1,s),{2,layout.buttons[0].x,layout.buttons[0].y}}),s);
            assert(padDirection(pad)==Direction::DownLeft && (pad.buttons & ButtonA));
            pad=m.update(fingers({{2,layout.buttons[0].x,layout.buttons[0].y},position(9,s)}),s);
            assert(padDirection(pad)==Direction::UpRight && (pad.buttons & ButtonA));
            pad=m.update(fingers({{2,layout.buttons[0].x,layout.buttons[0].y}}),s);
            assert(padDirection(pad)==Direction::Neutral && pad.buttons==ButtonA);
            assert(neutral(m.update({},s)));
            // New touches may start directly on a direction, without first centering.
            assert(padDirection(m.update(fingers({position(8,s)}),s))==Direction::Up);
            m.reset();
            m.update(fingers({position(5,s),position(6,s,2)}),s);
            assert(neutral(m.update(fingers({position(6,s,2)}),s))); // no takeover
            m.reset();
            m.update(fingers({{1,800,400}}),s);
            assert(neutral(m.update(fingers({position(6,s)}),s))); // must start inside
        }
    }
}
static void configuration() {
    Settings s; s.stick_mode=StickMode::Arcade8; s.arcade_deadzone=10; s.arcade_output=ArcadeOutput::LeftStick;
    Settings loaded;
    assert(parseSettings(formatSettings(s),loaded));
    assert(loaded.stick_mode==s.stick_mode && loaded.arcade_deadzone==10 && loaded.arcade_output==s.arcade_output);
    assert(parseSettings("version=1\nopacity=70\nscale=125\ndeadzone=25\n",loaded));
    assert(loaded.opacity==70 && loaded.scale==125 && loaded.deadzone==25);
    assert(loaded.stick_mode==StickMode::Analog && loaded.arcade_deadzone==20 && loaded.arcade_output==ArcadeOutput::Dpad);
    for (const char* tail:{"stick_mode=2\narcade_deadzone=20\narcade_output=0\n",
                          "stick_mode=1\narcade_deadzone=4\narcade_output=0\n",
                          "stick_mode=1\narcade_deadzone=31\narcade_output=0\n",
                          "stick_mode=1\narcade_deadzone=20\narcade_output=2\n",
                          "stick_mode=1\narcade_deadzone=20\n"}) {
        assert(!parseSettings(std::string("version=2\nopacity=40\nscale=100\ndeadzone=15\n")+tail,loaded));
    }
    assert(!parseSettings(formatSettings(s)+"stick_mode=1\n",loaded));
    auto bad=s; bad.reserved2=1; assert(!valid(bad));
    const std::string path="build/host/arcade-settings.ini";
    std::remove(path.c_str()); std::remove((path+".bak").c_str());
    assert(saveSettingsFile(path,s));
    assert(loadSettingsFile(path,loaded) && loaded.stick_mode==StickMode::Arcade8 && loaded.arcade_output==ArcadeOutput::LeftStick);
    s.arcade_output=ArcadeOutput::Dpad; assert(saveSettingsFile(path,s));
    assert(loadSettingsFile(path,loaded) && loaded.arcade_output==ArcadeOutput::Dpad);
    std::remove(path.c_str()); std::remove((path+".bak").c_str());
}
static void lifecycle() {
    Settings settings; settings.stick_mode=StickMode::Arcade8;
    Engine engine; engine.setSettings(settings);
    uint64_t now=1000; Sample sample; sample.available=true; sample.application_pid=42;
    auto tick=[&] { now+=16; ++sample.counter; engine.tick(sample,now); };
    auto activate=[&] {
        sample.touches={}; sample.home=false; sample.sleep=false; sample.tesla=false; sample.application_pid=42;
        assert(engine.prepare(false,42,now)==Error::Ok);
        assert(engine.commit(engine.state().token,now)==Error::Ok);
        tick(); assert(engine.state().mode==Mode::Active);
    };
    activate();
    const auto frozenDisplay=now;
    for (unsigned i=0;i<1500;++i) {
        const unsigned d=(i/11)%9+1;
        sample.touches=fingers({position(d,settings),{2,1175,540}}); tick();
        const auto check=checkWatchdogs(engine.state().mode,false,now,frozenDisplay,[&]{return now;});
        assert(check.reason==Reason::None && engine.state().mode==Mode::Active);
        assert(padDirection(engine.state().pad)==static_cast<Direction>(d));
        assert(engine.state().pad.buttons & ButtonA);
    }
    sample.touches={}; tick(); assert(neutral(engine.state().pad));
    for (unsigned event=0;event<6;++event) {
        activate(); sample.touches=fingers({position(3,settings)}); tick();
        assert(engine.state().pad.buttons==(ButtonDown|ButtonRight));
        switch (event) {
            case 0: sample.home=true; tick(); break;
            case 1: sample.sleep=true; tick(); break;
            case 2: sample.tesla=true; tick(); break;
            case 3: sample.application_pid=43; tick(); break;
            case 4: now+=250; engine.tick(sample,now); break;
            case 5: engine.setSettings(Settings{}); break;
        }
        assert(engine.state().mode==Mode::Paused && neutral(engine.state().pad));
    }
    // Switching to analog starts neutral and restores proportional movement.
    activate(); sample.touches=fingers({{1,237.5f,540}}); tick();
    assert(!(engine.state().pad.buttons & DirectionButtons) && std::abs(engine.state().pad.lx-16384)<=1);
}
static void drawing() {
    Settings s; s.stick_mode=StickMode::Arcade8;
    for (unsigned divisor:{1U,2U}) for (uint32_t scale:{75U,100U,125U}) {
        s.scale=scale;
        const unsigned width=1280/divisor,height=720/divisor;
        std::vector<uint16_t> pixels(width*height+2,0xBEEF), center;
        for (unsigned i=1;i<=9;++i) {
            PadState p; applyArcadeDirection(p,static_cast<Direction>(i),ArcadeOutput::Dpad);
            draw(pixels.data()+1,width,s,p,true,divisor);
            assert(pixels.front()==0xBEEF && pixels.back()==0xBEEF);
            assert(pixels[1+height/2*width+width/2]==0); // game center stays clear
            if (i!=5) {
                const auto c=Layout::make(s).stick;
                const auto axes=directionAxes(static_cast<Direction>(i));
                const float distance=c.radius*0.84f/(axes.x && axes.y ? 1.41421356f : 1.0f);
                const auto x=static_cast<unsigned>(std::lround((c.x+axes.x*distance)/divisor));
                const auto y=static_cast<unsigned>(std::lround((c.y-axes.y*distance)/divisor));
                const auto mark=pixels[1+y*width+x];
                assert((mark&15)==2 && ((mark>>4)&15)==12); // knob cannot obscure active marker
            }
            if (i==5) center=pixels;
            if (i>5) assert(pixels!=center);
        }
        draw(pixels.data()+1,width,s,{},false,divisor);
        for (size_t i=1;i<pixels.size()-1;++i) assert(pixels[i]==0);
    }
    s.scale=100;
    std::vector<uint16_t> pixels(1280*720);
    PadState p{ButtonA}; applyArcadeDirection(p,Direction::DownRight,ArcadeOutput::Dpad);
    draw(pixels.data(),1280,s,p,true);
    FILE* file=std::fopen("build/host/arcade-preview.ppm","wb"); assert(file);
    std::fprintf(file,"P6\n1280 720\n255\n");
    for (auto c:pixels) {
        unsigned a=(c>>12)&15;
        unsigned char rgb[3];
        for (unsigned channel=0;channel<3;++channel) rgb[channel]=(((c>>(channel*4))&15)*17*a+25*(15-a))/15;
        assert(std::fwrite(rgb,1,3,file)==3);
    }
    std::fclose(file);
}
int main() {
    sectors(); gestures(); configuration(); lifecycle(); drawing();
    std::puts("PASS: arcade 9 states, immediate boundaries, fighting paths, out-of-ring tracking, multitouch, migration, lifecycle and delayed-display input");
}

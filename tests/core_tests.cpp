// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch/core.hpp"
#include "touch/draw.hpp"
#include "common/config.hpp"
#include "common/storage.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <random>
#include <vector>

using namespace touchpad;
static unsigned checks = 0;
#define CHECK(expr) do { ++checks; if (!(expr)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expr); std::exit(1); } } while (0)
static Touches fingers(std::initializer_list<Point> points) {
    Touches t; t.count = static_cast<uint32_t>(points.size());
    unsigned i = 0; for (const auto& p : points) t.points[i++] = p; return t;
}
static bool neutral(PadState p) { return p.buttons == 0 && p.lx == 0 && p.ly == 0 && p.rx == 0 && p.ry == 0; }

static void gestures() {
    Mapper m; Settings settings;
    CHECK(neutral(m.update({},settings)));
    CHECK(neutral(m.update(fingers({{7,180,540}}),settings)));
    auto pad = m.update(fingers({{7,280,540},{9,1175,540}}),settings);
    CHECK(pad.lx == 32767 && pad.ly == 0 && pad.buttons == ButtonA);
    pad = m.update(fingers({{9,1175,540},{7,280,540}}),settings);
    CHECK(pad.lx == 32767 && pad.buttons == ButtonA); // reorder must not transfer ownership
    pad = m.update(fingers({{7,1100,465},{9,1100,615}}),settings);
    CHECK(pad.buttons == ButtonB); // captured stick finger cannot also press X
    CHECK(std::abs(std::hypot(pad.lx,pad.ly)-32767) < 1.5);
    pad = m.update(fingers({{9,1100,615}}),settings);
    CHECK(pad.lx == 0 && pad.ly == 0 && pad.buttons == ButtonB);
    pad = m.update(fingers({{9,1025,540}}),settings);
    CHECK(pad.buttons == ButtonY);
    pad = m.update(fingers({{9,1065,580}}),settings);
    CHECK(pad.buttons == 0); // gap between buttons releases
    pad = m.update(fingers({{9,1100,465},{10,1100,465}}),settings);
    CHECK(pad.buttons == ButtonX);
    pad = m.update(fingers({{10,1100,465}}),settings);
    CHECK(pad.buttons == ButtonX); // lifting one finger is not a release
    CHECK(neutral(m.update({},settings)));

    m.reset();
    m.update(fingers({{1,180,540},{2,190,540}}),settings);
    pad = m.update(fingers({{2,240,540}}),settings);
    CHECK(pad.lx == 0); // no jump to a pre-existing second finger
    m.update({},settings);
    pad = m.update(fingers({{2,280,540}}),settings);
    CHECK(pad.lx == 32767);
    m.reset();
    CHECK(neutral(m.update(fingers({{1,800,400}}),settings)));
    CHECK(neutral(m.update(fingers({{1,250,540}}),settings))); // new touch required for stick
    CHECK(m.update(fingers({{1,1175,540}}),settings).buttons == ButtonA);
    m.reset();
    CHECK(neutral(m.update(fingers({{1,195,540}}),settings))); // deadzone boundary
    pad = m.update(fingers({{1,237.5f,540}}),settings);
    CHECK(std::abs(pad.lx-16384) <= 1);
    pad = m.update(fingers({{1,180,440}}),settings);
    CHECK(pad.ly == 32767 && pad.lx == 0);
    pad = m.update(fingers({{1,180,640}}),settings);
    CHECK(pad.ly == -32767);
    CHECK(neutral(m.update(fingers({{1,180,540},{1,1175,540}}),settings)));
    CHECK(!valid(fingers({{1,std::numeric_limits<float>::quiet_NaN(),0}})));
    CHECK(!valid(fingers({{1,-1,0}})) && !valid(fingers({{1,1280,0}})));
    Touches oversized; oversized.count = 17; CHECK(!valid(oversized));
    for (auto scale : {75U,100U,125U}) {
        settings.scale = scale;
        auto l = Layout::make(settings);
        for (const auto& c : l.buttons) CHECK(c.x-c.radius >= 0 && c.x+c.radius < 1280 && c.y+c.radius < 720);
        m.reset(); pad = m.update(fingers({{1,l.stick.x+l.stick.radius,l.stick.y}}),settings);
        CHECK(pad.lx == 32767);
    }
    std::mt19937 random(20260922);
    settings = {};
    for (unsigned i = 0; i < 2000; ++i) {
        Touches t; t.count = random()%17;
        for (unsigned j = 0; j < t.count; ++j) t.points[j] = {j,static_cast<float>(random()%1280),static_cast<float>(random()%720)};
        pad = m.update(t,settings);
        CHECK(valid(pad)); CHECK(std::hypot(pad.lx,pad.ly) <= 32768);
    }
}
struct Fixture {
    Engine e;
    Sample s;
    uint64_t now = 1000;
    Fixture() { s.available = true; s.application_pid = 42; }
    void tick(unsigned delta = 16, bool fresh = true) { now += delta; if (fresh) ++s.counter; e.tick(s,now); }
    void activate() {
        CHECK(e.prepare(false,42,now) == Error::Ok);
        CHECK(e.commit(e.state().token,now) == Error::Ok);
        tick(); CHECK(e.state().mode == Mode::Active);
    }
    void press() { s.touches = fingers({{9,1175,540}}); tick(); CHECK(e.state().pad.buttons == ButtonA); }
};
static void lifecycle() {
    Fixture f;
    CHECK(f.e.prepare(false,0,f.now) == Error::NoApplication);
    CHECK(f.e.commit(99,f.now) == Error::InvalidTransition);
    f.s.touches = fingers({{2,1175,540}});
    CHECK(f.e.prepare(false,42,f.now) == Error::Ok);
    const auto old = f.e.state().token;
    CHECK(f.e.commit(old,f.now) == Error::Ok);
    f.tick(); CHECK(f.e.state().mode == Mode::WaitingTouch && neutral(f.e.state().pad));
    f.s.touches = {}; f.tick(); CHECK(f.e.state().mode == Mode::Active);
    f.press(); f.e.stop(Reason::User,true); CHECK(neutral(f.e.state().pad));
    CHECK(f.e.commit(old,f.now) == Error::InvalidTransition);
    f.tick(); CHECK(f.e.state().mode == Mode::Off);

    for (unsigned event = 0; event < 7; ++event) {
        Fixture g; g.activate(); g.press();
        switch (event) {
            case 0: g.s.home = true; break;
            case 1: g.s.sleep = true; break;
            case 2: g.s.tesla = true; break;
            case 3: g.s.docked = true; break;
            case 4: g.s.application_pid = 0; break;
            case 5: g.s.application_pid = 43; break;
            case 6: g.s.clock_gap = true; break;
        }
        g.tick(); CHECK(g.e.state().mode == Mode::Paused && neutral(g.e.state().pad));
        g.s = {}; g.s.available = true; g.s.application_pid = 42;
        g.tick(); CHECK(g.e.state().mode == Mode::Paused); // never resume by itself
    }
    {
        Fixture g; g.activate(); g.press();
        for (int i = 0; i < 16; ++i) g.tick(16,false);
        CHECK(neutral(g.e.state().pad) && g.e.state().reason == Reason::StaleInput);
    }
    {
        Fixture g; g.activate(); g.press(); g.s.available = false;
        for (int i = 0; i < 16; ++i) g.tick();
        CHECK(neutral(g.e.state().pad) && g.e.state().reason == Reason::StaleInput);
    }
    {
        Fixture g; g.activate(); g.press(); g.tick(250);
        CHECK(neutral(g.e.state().pad) && g.e.state().reason == Reason::ClockGap);
    }
    {
        Fixture g; g.activate(); g.press(); g.s.counter = 0; g.tick(16,false);
        CHECK(neutral(g.e.state().pad) && g.e.state().reason == Reason::StaleInput);
    }
    {
        Fixture g; g.activate(); g.press(); g.s.touches.count = 17; g.tick();
        CHECK(neutral(g.e.state().pad) && g.e.state().reason == Reason::InvalidInput);
    }
    {
        Fixture g; g.activate(); g.press(); Settings s; s.deadzone = 30; g.e.setSettings(s);
        CHECK(neutral(g.e.state().pad) && g.e.state().settings.deadzone == 30);
    }
    {
        Fixture g; CHECK(g.e.prepare(false,42,g.now) == Error::Ok);
        for (int i = 0; i < 313; ++i) g.tick();
        CHECK(g.e.state().reason == Reason::Timeout);
    }
    {
        Fixture g; g.s.application_pid = 0;
        CHECK(g.e.prepare(true,0,g.now) == Error::Ok);
        CHECK(g.e.commit(g.e.state().token,g.now) == Error::Ok);
        g.tick(); CHECK(g.e.state().mode == Mode::Pairing && g.e.state().pad.buttons == (ButtonL|ButtonR));
        for (int i = 0; i < 25; ++i) g.tick();
        CHECK(neutral(g.e.state().pad) && g.e.state().reason == Reason::PairComplete);
        for (int i = 0; i < 25; ++i) g.tick();
        CHECK(neutral(g.e.state().pad));
    }
}
static void configuration() {
    Settings s;
    CHECK(parseSettings(formatSettings(s),s));
    CHECK(!parseSettings("version=2\nopacity=40\nscale=100\ndeadzone=15\n",s));
    CHECK(!parseSettings("version=1\nopacity=40\nopacity=40\nscale=100\ndeadzone=15\n",s));
    CHECK(!parseSettings("version=1\nopacity=-1\nscale=100\ndeadzone=15\n",s));
    CHECK(!parseSettings("version=1\nopacity=99\nscale=100\ndeadzone=15\n",s));
    CHECK(!parseSettings("version=1\nopacity=40\nscale=100\n",s));
    CHECK(s.opacity == 40);
    CHECK(parseSettings("# settings\r\n version = 1\r\nopacity=80\nscale=125\ndeadzone=30\n",s));
    CHECK(s.opacity == 80 && s.scale == 125 && s.deadzone == 30);
    CHECK(parseCombo("L+DDOWN+RS",0) == ((1ULL<<6)|(1ULL<<15)|(1ULL<<5)));
    CHECK(parseCombo("L + ZR",0) == ((1ULL<<6)|(1ULL<<9)));
    CHECK(parseCombo("L+NOT_A_KEY",123) == (1ULL<<6) && parseCombo("",123) == 123);
    CHECK(parseCombo("l+ddown+rs",0) == parseCombo("L+DDOWN+RS",0));
    CHECK(parseCombo("SL+SR",0) == ((1ULL<<24)|(1ULL<<25)|(1ULL<<26)|(1ULL<<27)));
    Request r; CHECK(valid(r));
    r.version = 1; CHECK(!valid(r)); r.version=ProtocolVersion+1; CHECK(!valid(r));
    r = {}; r.size--; CHECK(!valid(r));
    r = {}; r.reserved = 1; CHECK(!valid(r)); r = {}; r.command = static_cast<Command>(99); CHECK(!valid(r));
    PadState p; p.lx = 32768; CHECK(!valid(p)); p = {}; p.rx = 1; CHECK(!valid(p));
    p = {}; p.buttons = ButtonL|ButtonR; CHECK(valid(p) && valid(p,true));
}
static void rendering() {
    // Half-resolution renderer must stay within its smaller buffer and preserve
    // the logical control positions after the VI layer scales it to the screen.
    std::vector<uint16_t> half(640*360+2,0xBEEF);
    for (unsigned scale : {75U,100U,125U}) {
        Settings s; s.scale = scale;
        draw(half.data()+1,640,s,{},true,2);
        CHECK(half.front() == 0xBEEF && half.back() == 0xBEEF);
        CHECK(half[1+50*640+320] == 0);
        const auto layout = Layout::make(s);
        CHECK(half[1+static_cast<unsigned>(layout.stick.y/2)*640+static_cast<unsigned>(layout.stick.x/2)] != 0);
        for (const auto& b : layout.buttons)
            CHECK(half[1+static_cast<unsigned>(b.y/2)*640+static_cast<unsigned>(b.x/2)] != 0);
    }
    draw(half.data()+1,640,{}, {},false,2);
    for (size_t i = 1; i <= 640*360; ++i) CHECK(half[i] == 0);
    std::vector<bool> halfVisited(640*384);
    for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 640; ++x) {
        const auto offset = tiledOffset(x,y,1280);
        CHECK(offset%2 == 0 && offset/2 < halfVisited.size() && !halfVisited[offset/2]);
        halfVisited[offset/2] = true;
    }
    constexpr size_t count = 1280*720;
    std::vector<uint16_t> storage(count+2,0xBEEF);
    for (unsigned scale : {75U,100U,125U}) {
        Settings s; s.scale = scale;
        draw(storage.data()+1,1280,s,{},true);
        CHECK(storage.front() == 0xBEEF && storage.back() == 0xBEEF);
        CHECK(storage[1+100*1280+640] == 0); // untouched center is transparent
    }
    draw(storage.data()+1,1280,{}, {},false);
    for (size_t i = 1; i <= count; ++i) CHECK(storage[i] == 0);
    std::vector<bool> visited(1280*768);
    for (unsigned y = 0; y < 720; ++y) for (unsigned x = 0; x < 1280; ++x) {
        const auto offset = tiledOffset(x,y,2560);
        CHECK(offset%2 == 0 && offset/2 < visited.size() && !visited[offset/2]);
        visited[offset/2] = true;
    }
    PadState p{ButtonA,18000,12000,0,0};
    draw(storage.data()+1,1280,{},p,true);
    FILE* out = std::fopen("build/host/layout.ppm","wb"); CHECK(out);
    std::fprintf(out,"P6\n1280 720\n255\n");
    for (unsigned y = 0; y < 720; ++y) for (unsigned x = 0; x < 1280; ++x) {
        const uint16_t v = storage[1+y*1280+x];
        const unsigned alpha = v>>12;
        unsigned char rgb[3];
        for (unsigned c = 0; c < 3; ++c) {
            unsigned bg = 24+c*7+y/30;
            rgb[c] = static_cast<unsigned char>((((v>>(4*c))&15)*17*alpha+bg*(15-alpha))/15);
        }
        std::fwrite(rgb,1,3,out);
    }
    std::fclose(out);
}
static void storage() {
    const std::string path = "build/host/test-settings.ini";
    std::remove(path.c_str()); std::remove((path+".bak").c_str()); std::remove((path+".tmp").c_str());
    Settings s, loaded;
    CHECK(saveSettingsFile(path,s)); CHECK(loadSettingsFile(path,loaded) && loaded.opacity == 40);
    s.opacity = 65;
    CHECK(saveSettingsFile(path,s)); CHECK(loadSettingsFile(path,loaded) && loaded.opacity == 65);
    // Simulate interrupted promotion: old file became .bak, new file is still .tmp.
    std::remove(path.c_str());
    CHECK(loadSettingsFile(path,loaded) && loaded.opacity == 40);
    CHECK(saveSettingsFile(path,s)); CHECK(loadSettingsFile(path,loaded) && loaded.opacity == 65);
    // Corrupt primary must recover the previous complete configuration.
    FILE* broken = std::fopen(path.c_str(),"wb"); CHECK(broken);
    std::fputs("version=1\nopacity=",broken); std::fclose(broken);
    CHECK(loadSettingsFile(path,loaded) && loaded.opacity == 40);
    s.opacity = 75;
    CHECK(saveSettingsFile(path,s)); CHECK(loadSettingsFile(path,loaded) && loaded.opacity == 75);
    s.opacity = 0; CHECK(!saveSettingsFile(path,s));
    CHECK(loadSettingsFile(path,loaded) && loaded.opacity == 75);
}
int main() {
    gestures(); lifecycle(); configuration(); storage(); rendering();
    std::printf("PASS: %u checks (gestures, state transitions, settings, protocol, renderer)\n",checks);
}

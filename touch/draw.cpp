// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch/draw.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace touchpad {
static uint16_t color(unsigned r, unsigned g, unsigned b, unsigned alpha) {
    return static_cast<uint16_t>(r | (g<<4) | (b<<8) | (alpha<<12));
}
static void circle(uint16_t* pixels, uint32_t stride, Circle c, uint16_t fill, uint16_t edge, unsigned divisor) {
    c.x /= divisor; c.y /= divisor; c.radius /= divisor;
    const int left = std::max(0, static_cast<int>(c.x-c.radius-1));
    const int right = std::min(static_cast<int>(1280/divisor)-1, static_cast<int>(c.x+c.radius+1));
    const int top = std::max(0, static_cast<int>(c.y-c.radius-1));
    const int bottom = std::min(static_cast<int>(720/divisor)-1, static_cast<int>(c.y+c.radius+1));
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const float d = (x-c.x)*(x-c.x) + (y-c.y)*(y-c.y);
        if (d <= c.radius*c.radius) pixels[y*stride+x] = d >= (c.radius-2.0f/divisor)*(c.radius-2.0f/divisor) ? edge : fill;
    }
}
static void letter(uint16_t* pixels, uint32_t stride, int cx, int cy, unsigned index, uint16_t c, int scale, unsigned divisor) {
    cx /= divisor; cy /= divisor; scale = std::max(1, scale/static_cast<int>(divisor));
    constexpr uint8_t letters[4][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {17,17,10,4,10,17,17}, {17,17,10,4,4,4,4}
    };
    for (int y = 0; y < 7; ++y) for (int x = 0; x < 5; ++x) {
        if (!(letters[index][y] & (1<<(4-x)))) continue;
        for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
            const int px = cx - 5*scale/2 + x*scale+dx, py = cy - 7*scale/2 + y*scale+dy;
            if (px >= 0 && px < static_cast<int>(1280/divisor) && py >= 0 && py < static_cast<int>(720/divisor)) pixels[py*stride+px] = c;
        }
    }
}
void draw(uint16_t* pixels, uint32_t stride, const Settings& s, const PadState& p, bool visible, unsigned divisor) {
    if ((divisor != 1 && divisor != 2) || stride < 1280/divisor) return;
    std::memset(pixels, 0, stride*(720/divisor)*sizeof(uint16_t));
    if (!visible || !valid(s)) return;
    const auto l = Layout::make(s);
    const unsigned a = std::max(1U, (s.opacity*15+50)/100);
    const auto fill = color(2,3,4,a), edge = color(11,12,13,a), text = color(15,15,15,std::min(15U,a+3));
    circle(pixels,stride,l.stick,fill,edge,divisor);
    Circle inner = l.stick;
    inner.radius *= s.deadzone/100.0f;
    circle(pixels,stride,inner,color(3,4,5,a),color(6,7,8,a),divisor);
    Circle knob = l.stick;
    knob.radius *= 0.32f;
    knob.x += p.lx/32767.0f * l.stick.radius*0.66f;
    knob.y -= p.ly/32767.0f * l.stick.radius*0.66f;
    circle(pixels,stride,knob,color(5,10,11,a+1),text,divisor);
    constexpr uint64_t bits[] = {ButtonA,ButtonB,ButtonX,ButtonY};
    for (unsigned i = 0; i < 4; ++i) {
        const auto& b = l.buttons[i];
        circle(pixels,stride,b,p.buttons & bits[i] ? color(2,12,10,std::min(15U,a+4)) : fill,edge,divisor);
        letter(pixels,stride,static_cast<int>(b.x),static_cast<int>(b.y),i,text,std::max(2,static_cast<int>(s.scale*4/100)),divisor);
    }
}
size_t tiledOffset(unsigned x, unsigned y, unsigned strideBytes) {
    // Tegra 16Bx2 ordering; framebufferCreate uses 16 GOBs (128 rows) per block.
    const unsigned bx = x * 2;
    return static_cast<size_t>(y/128)*strideBytes*128 + (bx/64)*8192 + ((y%128)/8)*512 +
           ((bx%64)/32)*256 + ((y%8)/2)*64 + ((bx%32)/16)*32 + (y%2)*16 + bx%16;
}
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "sysmodule/runtime.hpp"
#include "touch/draw.hpp"
#include <cstring>
#include <cstdlib>

extern "C" u64 __nx_vi_layer_id;
namespace touchpad {
static Result changeStack(ViLayer* layer, u32 stack, bool add) {
    const struct { u32 stack; u32 padding; u64 layer; } in{stack,0,layer->layer_id};
    return serviceDispatchIn(viGetSession_IManagerDisplayService(),add ? 6000 : 6001,in);
}
void renderThread(void*) {
    ViDisplay display{};
    ViLayer layer{};
    NWindow window{};
    Framebuffer fb{};
    LayerStacks stacks((1U<<ViLayerStack_Default)|(1U<<ViLayerStack_Lcd));
    auto change=[&](u32 stack,bool add) {
        const auto result=changeStack(&layer,stack,add);
        runtimeTrace.write(add ? "layer-stack-add" : "layer-stack-remove",stack,result);
        return result;
    };
    bool vi = false, disp = false, managed = false, opened = false;
    Result rc = viInitialize(ViServiceType_Manager);
    vi = R_SUCCEEDED(rc);
    if (R_SUCCEEDED(rc)) { rc = viOpenDefaultDisplay(&display); disp = R_SUCCEEDED(rc); }
    if (R_SUCCEEDED(rc)) { rc = viCreateManagedLayer(&display,static_cast<ViLayerFlags>(0),0,&__nx_vi_layer_id); managed = R_SUCCEEDED(rc); }
    if (R_SUCCEEDED(rc)) { rc = viCreateLayer(&display,&layer); opened = R_SUCCEEDED(rc); }
    if (R_SUCCEEDED(rc)) rc = viSetLayerScalingMode(&layer,ViScalingMode_FitToLayer);
    s32 z = 0;
    if (R_SUCCEEDED(rc)) rc = viGetZOrderCountMax(&display,&z);
    // Keep Tesla's own layer above the gameplay controls.
    if (R_SUCCEEDED(rc)) rc = viSetLayerZ(&layer,z > 0 ? z-1 : z);
    runtimeTrace.write("renderer-layer-id",layer.layer_id,rc);
    runtimeTrace.write("renderer-layer-z",static_cast<u64>(z > 0 ? z-1 : z));
    // No stack membership and no blank frame at startup. Merely transparent
    // content still participates in composition and can displace another layer.
    s32 width = 0, height = 0;
    if (R_SUCCEEDED(rc)) rc = viGetDisplayLogicalResolution(&display,&width,&height);
    if (R_SUCCEEDED(rc) && (width <= 0 || height <= 0)) rc = MAKERESULT(Module_Libnx,LibnxError_BadInput);
    if (R_SUCCEEDED(rc)) rc = viSetLayerSize(&layer,width,height);
    if (R_SUCCEEDED(rc)) rc = viSetLayerPosition(&layer,0,0);
    if (R_SUCCEEDED(rc)) rc = nwindowCreateFromLayer(&window,&layer);
    // Keep a spare buffer while the compositor owns the currently displayed one.
    // The Applet heap has enough space for both buffers plus the linear canvas.
    if (R_SUCCEEDED(rc)) rc = framebufferCreate(&fb,&window,640,360,PIXEL_FORMAT_RGBA_4444,2);
    auto* linear = R_SUCCEEDED(rc) ? static_cast<uint16_t*>(std::malloc(640*360*2)) : nullptr;
    if (R_SUCCEEDED(rc) && !linear) rc = MAKERESULT(Module_Libnx,LibnxError_OutOfMemory);
    runtimeTrace.write("renderer-created-double-buffer",0,rc);
    Status previous{};
    bool first = true;
    u64 lastDelayLog=0;
    while (R_SUCCEEDED(rc)) {
        const u64 started = nowMs();
        const auto s = snapshot();
        const bool visible = s.mode == Mode::Active;
        u64 hideGeneration;
        { Lock lock; hideGeneration=runtime.layer_hide.requested; }
        if (!visible) {
            rc=stacks.hide(change);
            if (R_FAILED(rc)) break;
            first=true;
            { Lock lock; runtime.layer_hide.acknowledge(hideGeneration); }
        }
        const bool changed = visible && (first ||
            std::memcmp(&previous.pad,&s.pad,sizeof(PadState)) || std::memcmp(&previous.settings,&s.settings,sizeof(Settings)));
        if (changed) {
            if (first) runtimeTrace.write("frame-transition-begin",1);
            // Reattach before dequeuing: a hidden consumer may still hold the
            // last displayed buffers and must be allowed to release them.
            rc=stacks.show(change);
            if (R_FAILED(rc)) break;
            // Refresh logical size for dock/undock transitions; rendering remains disabled while docked.
            s32 w = 0, h = 0;
            rc = viGetDisplayLogicalResolution(&display,&w,&h);
            if (R_SUCCEEDED(rc) && w > 0 && h > 0 && (w != width || h != height)) {
                rc = viSetLayerSize(&layer,w,h); width = w; height = h;
            }
            if (R_FAILED(rc)) break;
            s32 slot = -1;
            NvMultiFence fence{};
            rc = nwindowDequeueBuffer(&window,&slot,&fence);
            if (R_SUCCEEDED(rc)) rc = nvMultiFenceWait(&fence,100000);
            if (R_FAILED(rc)) break;
            // A stop may have happened while dequeue was waiting for the compositor.
            const auto latest = snapshot();
            if (latest.mode!=Mode::Active) {
                rc=nwindowCancelBuffer(&window,slot,nullptr);
                continue;
            }
            draw(linear,640,latest.settings,latest.pad,latest.mode == Mode::Active,2);
            auto* dest = static_cast<uint8_t*>(fb.buf) + slot*fb.fb_size;
            std::memset(dest,0,fb.fb_size);
            for (unsigned y = 0; y < 360; ++y) for (unsigned x = 0; x < 640; ++x) {
                const auto value = linear[y*640+x];
                std::memcpy(dest+tiledOffset(x,y,fb.stride),&value,sizeof(value));
            }
            armDCacheFlush(dest,fb.fb_size);
            rc = nwindowQueueBuffer(&window,slot,nullptr);
            if (first) runtimeTrace.write("frame-transition-end",1,rc);
            previous = latest; first = false;
        }
        if (R_FAILED(rc)) break;
        u64 renderGap=0;
        {
            Lock lock;
            runtime.renderer_ready = true;
            const auto heartbeat=nowMs();
            if (runtime.render_heartbeat && heartbeat>=runtime.render_heartbeat)
                renderGap=heartbeat-runtime.render_heartbeat;
            runtime.render_heartbeat = heartbeat;
            if (changed) runtime.render_period_us = static_cast<uint32_t>((nowMs()-started)*1000);
        }
        // Report slow drawing from this thread, never from the input sender.
        // SD writes may themselves wait; input delivery must remain independent.
        if (renderGap>=RenderDelayNoticeMs && (!lastDelayLog || nowMs()-lastDelayLog>=5000)) {
            lastDelayLog=nowMs();
            runtimeTrace.write("render-delay-ms",renderGap);
        }
        const auto used = nowMs()-started;
        if (used < 16) svcSleepThread((16-used)*1000000);
    }
    fail(rc,Reason::Renderer);
    const auto detach=stacks.hide(change);
    runtimeTrace.write("renderer-detach-on-exit",0,detach);
    if (stacks.empty()) { Lock lock; runtime.layer_hide.acknowledge(runtime.layer_hide.requested); }
    runtimeTrace.write("renderer-exit",0,rc);
    std::free(linear);
    framebufferClose(&fb);
    if (nwindowIsValid(&window)) nwindowClose(&window);
    if (opened) viCloseLayer(&layer);
    if (managed) { layer.layer_id = __nx_vi_layer_id; viDestroyManagedLayer(&layer); }
    if (disp) viCloseDisplay(&display);
    if (vi) viExit();
}
}

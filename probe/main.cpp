// SPDX-License-Identifier: GPL-2.0-or-later
#include <switch.h>

// Manual-launch process probe. Same System pool and kernel/service ACL as the
// Touchpad backend, but no service initialization, rendering, controller or SD IO.
extern "C" {
u32 __nx_applet_type = AppletType_None;
void __libnx_initheap() {
    alignas(0x1000) static u8 heap[64 * 1024];
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = reinterpret_cast<char*>(heap);
    fake_heap_end = fake_heap_start + sizeof(heap);
}
void __appInit() {}
void __appExit() {}
}

int main() {
    for (;;) svcSleepThread(1000000000);
}

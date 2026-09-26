// SPDX-License-Identifier: GPL-2.0-or-later
#include "sysmodule/runtime.hpp"
#include <algorithm>
#include <cstring>

namespace touchpad {
static Status dispatch(const Request& r) {
    if (!valid(r)) { auto s = snapshot(); s.error = Error::Protocol; return s; }
    Error error = Error::Ok;
    u64 hideTicket=0;
    if (r.command == Command::Configure) {
        if (!valid(r.settings)) error = Error::InvalidSettings;
        else {
            { Lock lock; runtime.engine.stop(Reason::SettingsChanged); }
            if (!saveSettings(r.settings)) error = Error::Storage;
            else { Lock lock; runtime.engine.setSettings(r.settings); }
        }
    } else if (r.command != Command::Status) {
        u64 pid = 0;
        if (r.command == Command::Prepare) pmdmntGetApplicationProcessId(&pid);
        Lock lock;
        switch (r.command) {
            case Command::Prepare:
            case Command::PreparePair:
                if (!runtime.ready || !runtime.renderer_ready) error = Error::Unavailable;
                else {
                    runtime.error = 0;
                    error = runtime.engine.prepare(r.command == Command::PreparePair,pid,nowMs());
                }
                break;
            case Command::Commit: error = runtime.engine.commit(r.token,nowMs()); break;
            case Command::Stop: runtime.engine.stop(Reason::User,true); break;
            case Command::Pause:
                if (runtime.engine.state().mode != Mode::Off && runtime.engine.state().mode != Mode::Paused)
                    runtime.engine.stop(Reason::Tesla);
                hideTicket=runtime.layer_hide.request();
                break;
            default: error = Error::Protocol; break;
        }
    }
    if (r.command == Command::Pause || r.command == Command::Prepare || r.command == Command::PreparePair) refreshCombo();
    if (hideTicket) {
        // Reply only after the render thread has removed the gameplay layer.
        // Its last frame may have been in flight when Pause arrived.
        const u64 deadline=nowMs()+400;
        bool hidden=false;
        do {
            { Lock lock; hidden=runtime.layer_hide.done(hideTicket); }
            if (hidden) break;
            svcSleepThread(1000000);
        } while (nowMs()<deadline);
        runtimeTrace.write("pause-layer-detached",hideTicket,hidden ? 0 : KERNELRESULT(TimedOut));
        if (!hidden) error=Error::Unavailable;
    }
    auto s = snapshot(); s.error = error; return s;
}
static void response(const void* data, size_t size, Result result = 0, u32 token = 0) {
    void* tls = armGetTls();
    std::memset(tls,0,0x100);
    HipcMetadata meta{};
    meta.num_data_words = static_cast<u32>((16+sizeof(CmifOutHeader)+size+3)/4);
    auto out = hipcMakeRequest(tls,meta);
    auto* header = static_cast<CmifOutHeader*>(cmifGetAlignedDataStart(out.data_words,tls));
    *header = {CMIF_OUT_HEADER_MAGIC,0,result,token};
    if (size) std::memcpy(header+1,data,size);
}
static bool process() {
    // Copy before any service calls overwrite TLS. Only inline, non-domain requests are accepted.
    alignas(16) uint8_t message[0x100];
    std::memcpy(message,armGetTls(),sizeof(message));
    HipcHeader raw{}; std::memcpy(&raw,message,sizeof(raw));
    if (raw.has_special_header) {
        HipcSpecialHeader special{}; std::memcpy(&special,message+8,4);
        size_t offset = 12 + (special.send_pid ? 8 : 0);
        for (unsigned i = 0; i < static_cast<unsigned>(special.num_copy_handles+special.num_move_handles); ++i) {
            Handle h = 0; std::memcpy(&h,message+offset,4); offset += 4;
            svcCloseHandle(h);
        }
    }
    if (raw.type == CmifCommandType_Close) return false;
    if (raw.has_special_header || raw.num_send_statics || raw.num_send_buffers || raw.num_recv_buffers ||
        raw.num_exch_buffers || raw.recv_static_mode || raw.num_data_words > 48 || raw.num_data_words < 8) {
        response(nullptr,0,MAKERESULT(Module_Libnx,LibnxError_BadInput)); return true;
    }
    auto parsed = hipcParseRequest(message);
    const auto* header = static_cast<CmifInHeader*>(cmifGetAlignedDataStart(parsed.data.data_words,message));
    const size_t dataEnd = reinterpret_cast<uint8_t*>(parsed.data.data_words)-message + raw.num_data_words*4;
    const size_t bodyOffset = reinterpret_cast<const uint8_t*>(header+1)-message;
    if (header->magic != CMIF_IN_HEADER_MAGIC || header->version > 1 || bodyOffset > dataEnd || dataEnd > sizeof(message)) {
        response(nullptr,0,MAKERESULT(Module_Libnx,LibnxError_BadInput)); return true;
    }
    if (raw.type == CmifCommandType_Control && header->command_id == 3) {
        const u16 pointerSize = 0;
        response(&pointerSize,sizeof(pointerSize)); return true;
    }
    if ((raw.type != CmifCommandType_Request && raw.type != CmifCommandType_RequestWithContext) || header->command_id != 0 ||
        raw.num_data_words != (16+sizeof(CmifInHeader)+sizeof(Request))/4 || bodyOffset+sizeof(Request) > dataEnd) {
        response(nullptr,0,MAKERESULT(Module_Libnx,LibnxError_BadInput)); return true;
    }
    Request request;
    std::memcpy(static_cast<void*>(&request),header+1,sizeof(request));
    if (request.command != Command::Status) runtimeTrace.write("command-begin",static_cast<u32>(request.command));
    const auto out = dispatch(request);
    if (request.command != Command::Status) {
        runtimeTrace.write("command-end",static_cast<u32>(request.command),static_cast<Result>(out.error));
        runtimeTrace.write("mode",static_cast<u32>(out.mode),out.backend_result);
    }
    response(&out,sizeof(out),0,header->token);
    return true;
}
void serverLoop() {
    Handle handles[5]{};
    Result rc = svcManageNamedPort(&handles[0],ServiceName,4);
    runtimeTrace.write("ipc-port-created",0,rc);
    if (R_FAILED(rc)) { fail(rc,Reason::Backend); return; }
    int count = 1;
    for (;;) {
        std::memset(armGetTls(),0,0x100);
        s32 index = -1;
        rc = svcReplyAndReceive(&index,handles,count,INVALID_HANDLE,1000000000);
        if (R_VALUE(rc) == 0xEA01) continue; // kernel timeout
        if (R_FAILED(rc)) {
            if (index > 0 && index < count) { svcCloseHandle(handles[index]); handles[index] = handles[--count]; continue; }
            fail(rc,Reason::Backend); break;
        }
        if (index == 0) {
            Handle session = 0;
            rc = svcAcceptSession(&session,handles[0]);
            if (R_SUCCEEDED(rc)) {
                if (count < 5) handles[count++] = session;
                else svcCloseHandle(session);
            }
            continue;
        }
        if (!process()) { svcCloseHandle(handles[index]); handles[index] = handles[--count]; continue; }
        s32 unused = -1;
        rc = svcReplyAndReceive(&unused,nullptr,0,handles[index],0);
        // After successfully replying, the empty wait returns Timeout. Other failures close only this session.
        if (R_FAILED(rc) && R_VALUE(rc) != 0xEA01) { svcCloseHandle(handles[index]); handles[index] = handles[--count]; }
    }
    for (int i = 0; i < count; ++i) svcCloseHandle(handles[i]);
}
}

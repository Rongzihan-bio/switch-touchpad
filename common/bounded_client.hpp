// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <switch.h>
#include <cstring>
#include "common/protocol.hpp"

namespace touchpad {
// For the Tesla/System process only: async IPC needs an event quota, which
// Application/Applet processes normally do not have. No worker can outlive NRO.
class BoundedClient {
    alignas(0x1000) unsigned char buffer_[0x1000]{};
    Handle session_ = INVALID_HANDLE, event_ = INVALID_HANDLE;
    bool failed_ = false;
public:
    // A timed-out request still owns this page. Never reuse it or unload the
    // overlay until the kernel signals completion. Closing an in-flight session
    // alone does not unlock a request already received by the server.
    bool canClose() {
        if (event_ == INVALID_HANDLE) return true;
        s32 index=0;
        if (R_FAILED(svcWaitSynchronization(&index,&event_,1,0))) return false;
        svcCloseHandle(event_); event_=INVALID_HANDLE;
        close();
        return true;
    }
    void close() {
        if (event_ != INVALID_HANDLE) return;
        if (session_ != INVALID_HANDLE) svcCloseHandle(session_);
        session_=INVALID_HANDLE;
    }
    Result call(const Request& request, Status& status) {
        if (!canClose()) return KERNELRESULT(TimedOut);
        if (failed_) return KERNELRESULT(TimedOut); // user must reopen this overlay
        if (!envIsSyscallHinted(0x23)) return MAKERESULT(Module_Libnx,LibnxError_IncompatSysVer);
        if (session_ == INVALID_HANDLE) {
            Result rc=svcConnectToNamedPort(&session_,ServiceName);
            if (R_FAILED(rc)) { session_=INVALID_HANDLE; return rc; }
        }
        std::memset(buffer_,0,sizeof(buffer_));
        CmifRequestFormat format{};
        format.request_id=0; format.data_size=sizeof(request);
        const auto message=cmifMakeRequest(buffer_,format);
        std::memcpy(message.data,&request,sizeof(request));
        Result rc=svcSendAsyncRequestWithUserBuffer(&event_,buffer_,sizeof(buffer_),session_);
        if (R_FAILED(rc)) { event_=INVALID_HANDLE; close(); return rc; }
        s32 index=0;
        rc=svcWaitSynchronization(&index,&event_,1,1000000000);
        if (R_FAILED(rc)) { failed_=true; return rc; }
        svcCloseHandle(event_); event_=INVALID_HANDLE;
        CmifResponse reply{};
        rc=cmifParseResponse(&reply,buffer_,false,sizeof(Status));
        if (R_SUCCEEDED(rc)) {
            Status next{}; std::memcpy(&next,reply.data,sizeof(next));
            if (next.version!=ProtocolVersion || next.size!=sizeof(Status))
                rc=MAKERESULT(Module_Libnx,LibnxError_BadInput);
            else status=next;
        }
        if (R_FAILED(rc)) close();
        return rc;
    }
};
}

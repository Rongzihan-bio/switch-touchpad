// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <switch.h>
#include "common/protocol.hpp"
namespace touchpad {
class Client {
    Service service_{};
public:
    ~Client() { close(); }
    void close() {
        // This named port is never a domain and has no remote objects to close.
        // Avoid a synchronous CMIF close request when the server is unresponsive.
        if (serviceIsActive(&service_)) svcCloseHandle(service_.session);
        service_={};
    }
    Result call(const Request& request, Status& status) {
        if (!serviceIsActive(&service_)) {
            Handle handle = INVALID_HANDLE;
            // A named port fails immediately if the backend is absent. SM GetService can wait forever.
            Result rc = svcConnectToNamedPort(&handle, ServiceName);
            if (R_FAILED(rc)) return rc;
            // No pointer buffers are used; serviceCreate would make an extra
            // synchronous query before the first actual status request.
            service_.session=handle; service_.own_handle=1;
        }
        Result rc = serviceDispatchInOut(&service_, 0, request, status);
        if (R_FAILED(rc)) { close(); return rc; }
        if (status.version != ProtocolVersion || status.size != sizeof(Status)) {
            close(); return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        return 0;
    }
};
}

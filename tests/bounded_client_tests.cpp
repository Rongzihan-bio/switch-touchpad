// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/bounded_client.hpp"
#include <cstdio>
int main() {
    using namespace touchpad;
    Request request{};
    Status status{};
    transport::reset();
    {
        BoundedClient client;
        transport::response.sequence=123;
        assert(client.call(request,status)==0 && status.sequence==123);
        assert(client.canClose()); client.close();
    }
    transport::reset();
    {
        BoundedClient client;
        transport::complete=false;
        assert(client.call(request,status)==KERNELRESULT(TimedOut));
        assert(!client.canClose());
        client.close(); // must leave the locked buffer and its handles alive
        request.command=Command::Stop;
        assert(client.call(request,status)==KERNELRESULT(TimedOut));
        assert(transport::sends==1 && transport::handles.size()==2);
        assert(static_cast<Request*>(transport::buffer)->command==Command::Status);
        transport::complete=true; // the server finally replies
        assert(client.canClose() && transport::handles.empty());
        assert(client.call(request,status)==KERNELRESULT(TimedOut));
        assert(transport::sends==1); // no automatic replay after timeout
    }
    transport::reset();
    {
        BoundedClient client;
        transport::response.version=999;
        const auto previous=status.sequence;
        assert(R_FAILED(client.call(request,status)) && status.sequence==previous);
        assert(transport::handles.empty() && client.canClose());
    }
    transport::reset();
    {
        BoundedClient client;
        transport::sendResult=0x10801; // event quota exhausted
        assert(client.call(request,status)==0x10801);
        assert(client.canClose() && transport::handles.empty());
    }
    transport::reset();
    {
        BoundedClient client;
        transport::available=false;
        assert(R_FAILED(client.call(request,status)));
        assert(transport::sends==0 && transport::handles.empty());
    }
    transport::reset();
    std::puts("PASS: IPC success, stalled server, delayed completion, no replay, version mismatch, quota failure and unavailable SVC");
}

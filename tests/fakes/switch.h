// Minimal deterministic transport for exercising BoundedClient's lifetime rules.
#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <set>
#include "common/protocol.hpp"
using u32=uint32_t; using s32=int32_t; using u64=uint64_t;
using Handle=u32; using Result=u32;
constexpr Handle INVALID_HANDLE=0;
constexpr Result KernelTimedOut=0xEA01;
#define KERNELRESULT(x) Kernel##x
#define R_FAILED(x) ((x)!=0)
#define R_SUCCEEDED(x) ((x)==0)
constexpr u32 Module_Libnx=345, LibnxError_BadInput=11, LibnxError_IncompatSysVer=14;
constexpr Result MAKERESULT(u32 m,u32 d) { return m|(d<<9); }
struct CmifRequestFormat { u32 request_id=0, data_size=0; };
struct CmifRequest { void* data; };
struct CmifResponse { void* data; };
namespace transport {
inline bool available=true, complete=true, inFlight=false;
inline Result connectResult=0, sendResult=0, parseResult=0;
inline unsigned sends=0;
inline void* buffer=nullptr;
inline touchpad::Status response;
inline std::set<Handle> handles;
inline void reset() {
    assert(handles.empty() && !inFlight);
    available=true; complete=true; connectResult=sendResult=parseResult=0;
    sends=0; buffer=nullptr; response={};
}
}
inline bool envIsSyscallHinted(unsigned n) { assert(n==0x23); return transport::available; }
inline Result svcConnectToNamedPort(Handle* h,const char*) {
    if (transport::connectResult) return transport::connectResult;
    *h=1; assert(transport::handles.insert(*h).second); return 0;
}
inline Result svcCloseHandle(Handle h) {
    if (h==2) assert(!transport::inFlight);
    assert(transport::handles.erase(h)==1); return 0;
}
inline CmifRequest cmifMakeRequest(void* data,CmifRequestFormat f) {
    assert(!transport::inFlight && f.data_size==sizeof(touchpad::Request));
    return {data};
}
inline Result svcSendAsyncRequestWithUserBuffer(Handle* h,void* data,size_t size,Handle s) {
    assert(!transport::inFlight && s==1 && size==4096 && (uintptr_t(data)%4096)==0);
    ++transport::sends;
    if (transport::sendResult) return transport::sendResult;
    *h=2; assert(transport::handles.insert(*h).second);
    transport::buffer=data; transport::inFlight=true; return 0;
}
inline Result svcWaitSynchronization(s32*,const Handle* h,int count,int64_t timeout) {
    assert(*h==2 && count==1 && (timeout==0 || timeout==1000000000));
    if (!transport::complete) return KernelTimedOut;
    std::memcpy(transport::buffer,&transport::response,sizeof(transport::response));
    transport::inFlight=false; return 0;
}
inline Result cmifParseResponse(CmifResponse* out,void* data,bool domain,u32 size) {
    assert(!transport::inFlight && !domain && size==sizeof(touchpad::Status));
    out->data=data; return transport::parseResult;
}

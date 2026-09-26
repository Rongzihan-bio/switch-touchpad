// SPDX-License-Identifier: GPL-2.0-or-later
#include <switch.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr u64 Target = 0x4200000054504144ULL;
constexpr u64 Probe = 0x4200000054505052ULL;
constexpr const char* Startup = "sdmc:/config/touchpad/startup.log";
Mutex linesMutex{};
std::vector<std::string> lines;
std::string phase="starting diagnostics", launchSummary="not requested";
std::atomic<int> job{0}; // 0 idle, 1 collect, 2 Touchpad, 3 stop, 4 probe
std::atomic<bool> busy{true}, attempted{false}, attemptedProbe{false}, logOk{false};
FILE* report = nullptr;
char reportPath[160]{};
AppletType appletType;

void log(const char* fmt, ...) {
    char text[384];
    va_list args; va_start(args,fmt); std::vsnprintf(text,sizeof(text),fmt,args); va_end(args);
    // File text must never be interpreted as terminal escapes.
    for (char* p=text; *p; ++p) if (static_cast<unsigned char>(*p)<32 || static_cast<unsigned char>(*p)>126) *p='?';
    if (report && (std::fprintf(report,"%s\n",text)<0 || std::fflush(report)!=0)) logOk=false;
    mutexLock(&linesMutex);
    if (lines.size()>=1024) lines.erase(lines.begin(),lines.begin()+128);
    lines.emplace_back(text);
    mutexUnlock(&linesMutex);
}
void checkpoint(const char* stage) {
    mutexLock(&linesMutex); phase=stage; mutexUnlock(&linesMutex);
    log("STAGE %s",stage);
    if (report && fsync(fileno(report))!=0) logOk=false;
}
void result(const char* stage, Result rc) {
    log("%s: %08X (module=%u description=%u)",stage,rc,R_MODULE(rc),R_DESCRIPTION(rc));
}
void summary(const char* stage, Result rc) {
    char text[112]; std::snprintf(text,sizeof(text),"%s: %08X",stage,rc);
    mutexLock(&linesMutex); launchSummary=text; mutexUnlock(&linesMutex);
}
void notLaunched(const char* why) {
    mutexLock(&linesMutex); launchSummary=std::string("NOT LAUNCHED: ")+why; mutexUnlock(&linesMutex);
}
bool hinted(unsigned n) {
    if (envIsSyscallHinted(n)) return true;
    log("SVC %02X unavailable: loader did not grant a hint; skipped",n); return false;
}
std::string readStartup() {
    FILE* f=std::fopen(Startup,"rb");
    if (!f) return {};
    char buf[4097]{}; const auto n=std::fread(buf,1,4096,f); std::fclose(f);
    return std::string(buf,n);
}
void startupText(const std::string& text) {
    if (text.empty()) { log("startup.log: absent, empty or unreadable"); return; }
    size_t pos=0;
    while (pos<text.size()) {
        auto end=text.find('\n',pos); if (end==std::string::npos) end=text.size();
        log("backend-log: %s",text.substr(pos,end-pos).c_str()); pos=end+1;
    }
}
void fileInfo(const char* path) {
    struct stat st{};
    if (stat(path,&st)!=0) { log("FILE %s: unavailable errno=%d",path,errno); return; }
    log("FILE %s size=%lld mtime=%lld",path,static_cast<long long>(st.st_size),static_cast<long long>(st.st_mtime));
    if (!S_ISREG(st.st_mode) || st.st_size>16*1024*1024) { log("hash skipped: not regular or over 16 MiB"); return; }
    FILE* f=std::fopen(path,"rb");
    if (!f) { log("open failed errno=%d",errno); return; }
    Sha256Context sha; sha256ContextCreate(&sha);
    unsigned char buf[4096]; size_t n;
    while ((n=std::fread(buf,1,sizeof(buf),f))) sha256ContextUpdate(&sha,buf,n);
    const bool error=std::ferror(f); std::fclose(f);
    if (error) { log("hash read failed"); return; }
    unsigned char hash[32]; char hex[65]{}; sha256ContextGetHash(&sha,hash);
    for (unsigned i=0;i<32;++i) std::snprintf(hex+i*2,3,"%02x",hash[i]);
    log("SHA256 %s",hex);
}
void pmResources() {
    // Atmosphere 1.11.2 pm:dmnt command 65001. PM aborts on invalid indices;
    // keep the only requested groups (System=0, Applet=2) and resources fixed.
    Result rc=pmdmntInitialize(); result("PM quota pm:dmnt init",rc);
    if (R_FAILED(rc)) return;
    const char* names[]={"memory bytes","threads","events","transfer memories","sessions"};
    for (u32 group : {0u,2u}) {
        for (u32 resource=0;resource<5;++resource) {
            const struct { u32 group; u32 resource; } in{group,resource};
            struct { s64 current; s64 limit; } out{};
            static_assert(sizeof(in)==8 && sizeof(out)==16);
            rc=serviceDispatchInOut(pmdmntGetServiceSession(),65001,in,out);
            if (R_FAILED(rc)) {
                log("PM group=%u resource=%u unavailable rc=%08X; remaining queries skipped",group,resource,rc);
                pmdmntExit(); return;
            }
            log("PM %s %s: used=%lld limit=%lld available=%lld",
                group==0 ? "System" : "Applet",names[resource],
                static_cast<long long>(out.current),static_cast<long long>(out.limit),
                static_cast<long long>(out.limit-out.current));
        }
    }
    pmdmntExit();
}
void resources() {
    checkpoint("resource snapshot");
    log("SELF values belong to Diag's resource group, NOT Touchpad's.");
    log("Physical pool availability does NOT equal PM resource quota.");
    if (hinted(0x29)) {
        for (auto item : {InfoType_TotalMemorySize,InfoType_UsedMemorySize,InfoType_FreeThreadCount}) {
            u64 value=0; Result rc=svcGetInfo(&value,item,CUR_PROCESS_HANDLE,0);
            if (R_SUCCEEDED(rc)) log("SELF GetInfo[%u]=%llu",static_cast<unsigned>(item),static_cast<unsigned long long>(value));
            else result("SELF GetInfo unavailable",rc);
        }
        if (envIsSyscallHinted(0x30) && envIsSyscallHinted(0x31)) {
            u64 value=0; Result rc=svcGetInfo(&value,InfoType_ResourceLimit,INVALID_HANDLE,0);
            result("SELF resource handle",rc);
            if (R_SUCCEEDED(rc)) {
                const Handle h=static_cast<Handle>(value);
                const char* names[]={"memory bytes","threads","events","transfer memories","sessions"};
                for (unsigned i=0;i<5;++i) {
                    s64 limit=0, used=0;
                    Result lr=svcGetResourceLimitLimitValue(&limit,h,static_cast<LimitableResource>(i));
                    Result ur=svcGetResourceLimitCurrentValue(&used,h,static_cast<LimitableResource>(i));
                    if (R_SUCCEEDED(lr) && R_SUCCEEDED(ur)) log("SELF %s: used=%lld limit=%lld",names[i],static_cast<long long>(used),static_cast<long long>(limit));
                    else { result("SELF limit query",lr); result("SELF usage query",ur); }
                }
                svcCloseHandle(h);
            }
        } else log("SELF resource counters: SVC hints missing; skipped");
    }
    if (hinted(0x6F)) {
        const char* names[]={"Application","Applet","System","SystemUnsafe"};
        for (unsigned i=0;i<4;++i) {
            u64 total=0,used=0;
            Result tr=svcGetSystemInfo(&total,SystemInfoType_TotalPhysicalMemorySize,INVALID_HANDLE,i);
            Result ur=svcGetSystemInfo(&used,SystemInfoType_UsedPhysicalMemorySize,INVALID_HANDLE,i);
            if (R_SUCCEEDED(tr) && R_SUCCEEDED(ur)) log("POOL %s: total=%llu used=%llu bytes",names[i],static_cast<unsigned long long>(total),static_cast<unsigned long long>(used));
            else { log("POOL %s unavailable",names[i]); result("total query",tr); result("used query",ur); }
        }
    }
    pmResources();
}
void processes() {
    checkpoint("process snapshot");
    if (!hinted(0x65)) return;
    u64 pids[256]{}; s32 count=0;
    Result rc=svcGetProcessList(&count,pids,256); result("GetProcessList",rc);
    if (R_FAILED(rc)) return;
    log("process count=%d; at most 256 listed",count);
    Result pr=pminfoInitialize(); result("pm:info init",pr);
    for (s32 i=0;i<std::min(count,256);++i) {
        u64 program=0; Result map=R_SUCCEEDED(pr) ? pminfoGetProgramId(&program,pids[i]) : pr;
        if (R_SUCCEEDED(map)) log("PID %llu program %016llX",static_cast<unsigned long long>(pids[i]),static_cast<unsigned long long>(program));
        else log("PID %llu program unavailable rc=%08X",static_cast<unsigned long long>(pids[i]),map);
    }
    if (R_SUCCEEDED(pr)) pminfoExit();
}
Result targetPid(u64& pid, u64 program=Target) {
    Result rc=pmdmntInitialize(); result("pm:dmnt init",rc);
    if (R_SUCCEEDED(rc)) { rc=pmdmntGetProcessId(&pid,program); result("PID lookup",rc); pmdmntExit(); }
    if (R_SUCCEEDED(rc)) log("program %016llX PID=%llu",static_cast<unsigned long long>(program),static_cast<unsigned long long>(pid));
    return rc;
}
bool portPresent() {
    Handle h=INVALID_HANDLE; Result rc=svcConnectToNamedPort(&h,"touchpad");
    result("Touchpad named port connect (no IPC)",rc);
    if (R_SUCCEEDED(rc)) svcCloseHandle(h);
    return R_SUCCEEDED(rc);
}
void collect() {
    checkpoint("read-only collection");
    const auto v=hosversionGet();
    log("HOS cached=%u.%u.%u Atmosphere=%u appletType=%u",HOSVER_MAJOR(v),HOSVER_MINOR(v),HOSVER_MICRO(v),hosversionIsAtmosphere(),static_cast<unsigned>(appletType));
    Result rc=setsysInitialize(); result("set:sys init",rc);
    if (R_SUCCEEDED(rc)) {
        SetSysFirmwareVersion fw{}; rc=setsysGetFirmwareVersion(&fw); result("firmware query",rc);
        if (R_SUCCEEDED(rc)) log("HOS reported=%u.%u.%u display=%.24s",fw.major,fw.minor,fw.micro,fw.display_version);
        setsysExit();
    }
    log("AMS exact version: not inferred from HOS; provide separately.");
    fileInfo("sdmc:/atmosphere/contents/4200000054504144/exefs.nsp");
    fileInfo("sdmc:/atmosphere/contents/4200000054505052/exefs.nsp");
    fileInfo("sdmc:/switch/.overlays/touchpad.ovl");
    log("Expected rc10 backend: Applet physical pool=1, PM group=2, fixed heap=4 MiB.");
    log("PM counters are shared group totals; other processes can change them.");
    if (appletType!=AppletType_Application && appletType!=AppletType_SystemApplication)
        log("Applet-mode Diag consumes the tested pool; use application-mode hbmenu for A.");
    struct stat st{};
    const int flag=stat("sdmc:/atmosphere/contents/4200000054504144/flags/boot2.flag",&st);
    log("boot2.flag: %s (errno=%d)",flag==0 ? "PRESENT" : "not accessible",flag==0 ? 0 : errno);
    resources(); processes();
    u64 pid=0; targetPid(pid); portPresent();
    log("Existing startup.log may be stale:"); startupText(readStartup());
    checkpoint("collection complete; A tests Applet-pool Touchpad");
}
void probe() {
    checkpoint("minimal System-pool probe");
    if (!logOk) { notLaunched("probe: log unavailable"); return; }
    u64 existing=0;
    if (R_SUCCEEDED(targetPid(existing))) {
        log("SKIP: Touchpad is running; reboot before a separate legacy probe test");
        notLaunched("probe: Touchpad running"); return;
    }
    if (R_SUCCEEDED(targetPid(existing,Probe))) {
        log("SKIP: probe is already running; do not start twice");
        notLaunched("probe already running"); return;
    }
    resources();
    Result rc=pmshellInitialize(); result("probe pm:shell init",rc);
    if (R_FAILED(rc)) { summary("probe pm:shell init",rc); return; }
    NcmProgramLocation location{}; location.program_id=Probe; location.storageID=NcmStorageId_None;
    checkpoint("calling LaunchProgram for tiny System-pool probe");
    if (!logOk) { pmshellExit(); notLaunched("probe: log sync failed"); return; }
    u64 pid=0; rc=pmshellLaunchProgram(0,&location,&pid);
    result("probe LaunchProgram",rc); pmshellExit();
    summary("probe LaunchProgram returned",rc);
    if (R_SUCCEEDED(rc)) log("probe returned PID=%llu; stays idle until reboot",static_cast<unsigned long long>(pid));
    svcSleepThread(500000000);
    targetPid(existing,Probe); resources();
    checkpoint("probe finished; reboot clears an idle probe");
}
void launch() {
    checkpoint("manual launch test");
    if (!logOk) { log("BLOCKED: no writable diagnostic log; launch not attempted"); notLaunched("log unavailable"); return; }
    if (appletType!=AppletType_Application && appletType!=AppletType_SystemApplication) {
        log("BLOCKED: reopen Diag through application-mode hbmenu to avoid occupying the Applet pool");
        notLaunched("hold R while starting a game to open hbmenu"); return;
    }
    u64 existing=0;
    if (R_SUCCEEDED(targetPid(existing)) || portPresent()) { log("SKIP: backend already present; no launch or termination"); notLaunched("backend already running"); return; }
    if (R_SUCCEEDED(targetPid(existing,Probe))) {
        log("BLOCKED: legacy System-pool probe is running; reboot before the rc10 launch test");
        notLaunched("reboot to clear legacy probe"); return;
    }
    const auto before=readStartup();
    resources();
    checkpoint("connecting pm:shell");
    Result rc=pmshellInitialize(); result("pm:shell init",rc);
    summary("pm:shell init",rc);
    if (R_FAILED(rc)) return;
    NcmProgramLocation location{}; location.program_id=Target; location.storageID=NcmStorageId_None;
    u64 pid=0;
    checkpoint("calling LaunchProgram flags=0 storage=None");
    if (!logOk) { pmshellExit(); log("BLOCKED: log write failed before launch"); notLaunched("log sync failed"); return; }
    rc=pmshellLaunchProgram(0,&location,&pid);
    result("LaunchProgram",rc); pmshellExit();
    summary("LaunchProgram returned",rc);
    if (R_SUCCEEDED(rc)) log("PM returned success PID=%llu; initialization not yet confirmed",static_cast<unsigned long long>(pid));
    else if (R_VALUE(rc)==0x10801) log("LimitReached: resource category is NOT identified by this result.");
    checkpoint("launch returned");
    // Observe once immediately and once after a bounded settling period. No retry.
    u64 observed=0; targetPid(observed); portPresent();
    svcSleepThread(2000000000);
    targetPid(observed); portPresent();
    const auto after=readStartup();
    log("startup.log content %s; identical content is NOT proof of no execution",after==before ? "unchanged" : "changed");
    startupText(after); resources(); processes();
    checkpoint("launch test finished; backend left untouched");
}
void worker(void*) {
    Result sm=smInitialize(); result("diagnostic SM reference",sm);
    if (R_SUCCEEDED(sm)) collect();
    busy=false;
    for (;;) {
        const int request=job.exchange(0);
        if (request==3) break;
        if (request) {
            if (R_SUCCEEDED(sm)) {
                if (request==1) collect(); else if (request==2) launch(); else if (request==4) probe();
            }
            else log("Cannot query services: SM init failed");
            busy=false;
        }
        svcSleepThread(20000000);
    }
    if (R_SUCCEEDED(sm)) smExit();
}
}

// Requested heap size; hbloader can supply an inherited heap override instead.
extern "C" { u32 __nx_heap_size=16*1024*1024; }
int main() {
    consoleInit(nullptr);
    appletType=appletGetAppletType();
    mkdir("sdmc:/config",0777); mkdir("sdmc:/config/touchpad",0777); mkdir("sdmc:/config/touchpad/diagnostics",0777);
    for (unsigned i=0;i<32 && !report;++i) {
        std::snprintf(reportPath,sizeof(reportPath),"sdmc:/config/touchpad/diagnostics/diag-%016llx-%u.log",static_cast<unsigned long long>(armGetSystemTick()),i);
        int fd=open(reportPath,O_WRONLY|O_CREAT|O_EXCL,0666);
        if (fd>=0) { report=fdopen(fd,"w"); if (!report) close(fd); }
        else if (errno!=EEXIST) break;
    }
    logOk=report!=nullptr;
    log("Touchpad Diag 0.3.4; independent launcher; no auto-launch");
    log("Run in application mode; diagnostic app changes the resource baseline.");
    Thread thread{};
    Result rc=threadCreate(&thread,worker,nullptr,nullptr,0x10000,0x3B,-2);
    bool created=R_SUCCEEDED(rc);
    if (created) rc=threadStart(&thread);
    const bool started=R_SUCCEEDED(rc);
    if (!started) { result("diagnostic worker creation/start",rc); busy=false; }
    PadState pad; padConfigureInput(1,HidNpadStyleSet_NpadStandard); padInitializeDefault(&pad);
    size_t page=0; bool follow=true;
    while (appletMainLoop()) {
        padUpdate(&pad); const auto down=padGetButtonsDown(&pad);
        if ((down&HidNpadButton_Plus) && !busy) break;
        if (started && !busy && (down&HidNpadButton_A) && !attempted) { attempted=true; busy=true; job=2; follow=true; }
        if (started && !busy && (down&HidNpadButton_B) && !attemptedProbe) { attemptedProbe=true; busy=true; job=4; follow=true; }
        if (started && !busy && (down&HidNpadButton_X)) { busy=true; job=1; follow=true; }
        mutexLock(&linesMutex);
        const size_t pages=std::max<size_t>(1,(lines.size()+17)/18);
        if (down&HidNpadButton_AnyUp) { follow=false; if (page) --page; }
        if (down&HidNpadButton_AnyDown) { follow=false; page=std::min(page+1,pages-1); }
        page=follow ? pages-1 : std::min(page,pages-1);
        std::printf("\x1b[2J\x1b[HTouchpad Diag 0.3.4  |  %s\n",busy ? "COLLECTING / CALL IN PROGRESS" : "READY");
        std::printf("Mode: %s  |  A: launch   X: refresh   +: exit\n",
            (appletType==AppletType_Application || appletType==AppletType_SystemApplication) ? "APPLICATION" : "APPLET - A disabled");
        std::printf("Up/Down: pages   %zu/%zu   Launch used: %s\n",page+1,pages,attempted ? "yes" : "no");
        std::printf("Last stage: %.66s\n",phase.c_str());
        std::printf("Launch: %.70s\n",launchSummary.c_str());
        std::printf("Log: %s\n",logOk ? "config/touchpad/diagnostics/ (unique file per run)" : "UNAVAILABLE - launch disabled");
        std::printf("PM Applet = backend group; POOL = physical memory. Full values in log.\n\n");
        for (size_t i=page*18;i<std::min(lines.size(),page*18+18);++i) std::printf("%.78s\n",lines[i].c_str());
        mutexUnlock(&linesMutex);
        consoleUpdate(nullptr);
    }
    // A pending synchronous service call must return before freeing its state.
    if (started) { job=3; threadWaitForExit(&thread); }
    if (created) threadClose(&thread);
    if (report) { std::fflush(report); fsync(fileno(report)); std::fclose(report); }
    consoleExit(nullptr); return 0;
}

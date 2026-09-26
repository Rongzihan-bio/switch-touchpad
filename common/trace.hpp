// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <switch.h>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

namespace touchpad {
// Log transitions and calls which may block, never per-frame touch/button data.
class Trace {
    Mutex mutex_{};
    FILE* file_ = nullptr;
public:
    void open(const char* path, const char* previous) {
        mkdir("sdmc:/config",0777);
        mkdir("sdmc:/config/touchpad",0777);
        std::remove(previous);
        std::rename(path,previous);
        file_ = std::fopen(path,"w");
    }
    void write(const char* stage, u64 detail=0, Result rc=0) {
        mutexLock(&mutex_);
        if (file_) {
            std::fprintf(file_,"%llu %s detail=%llu rc=%08X\n",
                static_cast<unsigned long long>(armTicksToNs(armGetSystemTick())/1000000),stage,
                static_cast<unsigned long long>(detail),rc);
            std::fflush(file_);
            fsync(fileno(file_));
        }
        mutexUnlock(&mutex_);
    }
    void close() {
        mutexLock(&mutex_);
        if (file_) { std::fclose(file_); file_=nullptr; }
        mutexUnlock(&mutex_);
    }
};
}

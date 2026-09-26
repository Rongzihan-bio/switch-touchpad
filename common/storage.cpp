// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/storage.hpp"
#include "common/config.hpp"
#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <sys/stat.h>

namespace touchpad {
static bool loadOne(const std::string& path, Settings& settings) {
    FILE* f = std::fopen(path.c_str(),"rb");
    if (!f) return false;
    char bytes[1025];
    const size_t n = std::fread(bytes,1,sizeof(bytes),f);
    const bool ok = n <= 1024 && !std::ferror(f);
    std::fclose(f);
    return ok && parseSettings(std::string_view(bytes,n),settings);
}
bool loadSettingsFile(const std::string& path, Settings& settings) {
    return loadOne(path,settings) || loadOne(path+".bak",settings);
}
bool saveSettingsFile(const std::string& path, const Settings& settings) {
    if (!valid(settings)) return false;
    const std::string tmp = path+".tmp", bak = path+".bak";
    FILE* f = std::fopen(tmp.c_str(),"wb");
    if (!f) return false;
    const auto text = formatSettings(settings);
    bool ok = std::fwrite(text.data(),1,text.size(),f) == text.size();
    if (std::fflush(f) != 0) ok = false;
    if (fsync(fileno(f)) != 0) ok = false;
    if (std::fclose(f) != 0) ok = false;
    if (!ok) return false;
    // Horizon RenameFile refuses an existing destination. Keep a last-good backup
    // across the two renames so a reboot in between them can still recover settings.
    struct stat st{};
    const bool exists = stat(path.c_str(),&st) == 0;
    if (!exists && errno != ENOENT) return false;
    if (exists) {
        Settings previous;
        if (loadOne(path,previous)) {
            if (std::remove(bak.c_str()) != 0 && errno != ENOENT) return false;
            if (std::rename(path.c_str(),bak.c_str()) != 0) return false;
        } else if (std::remove(path.c_str()) != 0) return false;
    }
    if (std::rename(tmp.c_str(),path.c_str()) == 0) return true;
    // Best effort restore; .bak itself remains a valid boot-time fallback if this fails.
    std::rename(bak.c_str(),path.c_str());
    return false;
}
}

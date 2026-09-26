// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/config.hpp"
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>

namespace touchpad {
static std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
bool parseSettings(std::string_view text, Settings& result) {
    Settings next{};
    unsigned seen = 0;
    uint32_t version = 0;
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) return false;
        const auto key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (value.empty() || value.size() > 3 || value.find_first_not_of("0123456789") != std::string::npos) return false;
        const auto n = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        unsigned bit = 0;
        if (key == "version") { if (n != 1 && n != 2) return false; version = n; bit = 1; }
        else if (key == "opacity") { next.opacity = n; bit = 2; }
        else if (key == "scale") { next.scale = n; bit = 4; }
        else if (key == "deadzone") { next.deadzone = n; bit = 8; }
        else if (key == "stick_mode") { next.stick_mode = static_cast<StickMode>(n); bit = 16; }
        else if (key == "arcade_deadzone") { next.arcade_deadzone = n; bit = 32; }
        else if (key == "arcade_output") { next.arcade_output = static_cast<ArcadeOutput>(n); bit = 64; }
        else return false;
        if (seen & bit) return false;
        seen |= bit;
    }
    if (seen != (version == 1 ? 15U : 127U) || !valid(next)) return false;
    result = next;
    return true;
}
std::string formatSettings(const Settings& s) {
    return "version=2\nopacity=" + std::to_string(s.opacity) + "\nscale=" + std::to_string(s.scale) +
           "\ndeadzone=" + std::to_string(s.deadzone) +
           "\nstick_mode=" + std::to_string(static_cast<uint32_t>(s.stick_mode)) +
           "\narcade_deadzone=" + std::to_string(s.arcade_deadzone) +
           "\narcade_output=" + std::to_string(static_cast<uint32_t>(s.arcade_output)) + "\n";
}
uint64_t parseCombo(std::string_view text, uint64_t fallback) {
    // Match Tesla's case-insensitive key vocabulary and ignore unknown tokens, like libtesla.
    static const std::unordered_map<std::string, uint64_t> bits = {
        {"A",1ULL<<0},{"B",1ULL<<1},{"X",1ULL<<2},{"Y",1ULL<<3},
        {"LS",1ULL<<4},{"RS",1ULL<<5},{"L",1ULL<<6},{"R",1ULL<<7},
        {"ZL",1ULL<<8},{"ZR",1ULL<<9},{"PLUS",1ULL<<10},{"MINUS",1ULL<<11},
        {"SL",(1ULL<<24)|(1ULL<<26)},{"SR",(1ULL<<25)|(1ULL<<27)},
        {"DLEFT",1ULL<<12},{"DUP",1ULL<<13},{"DRIGHT",1ULL<<14},{"DDOWN",1ULL<<15}
    };
    uint64_t out = 0;
    std::istringstream in{std::string(text)};
    std::string word;
    while (std::getline(in, word, '+')) {
        word = trim(word);
        std::transform(word.begin(),word.end(),word.begin(),[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        auto found = bits.find(word);
        if (found != bits.end()) out |= found->second;
    }
    return out ? out : fallback;
}
}

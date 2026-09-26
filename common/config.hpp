// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/protocol.hpp"
#include <string>
#include <string_view>
namespace touchpad {
bool parseSettings(std::string_view text, Settings& result);
std::string formatSettings(const Settings& settings);
uint64_t parseCombo(std::string_view text, uint64_t fallback);
}

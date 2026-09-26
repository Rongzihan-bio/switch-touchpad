// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/protocol.hpp"
#include <string>
namespace touchpad {
bool loadSettingsFile(const std::string& path, Settings& settings);
bool saveSettingsFile(const std::string& path, const Settings& settings);
}

// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace helix {

/// First whitespace-delimited token of each non-blank line, upper-cased.
inline std::vector<std::string> gcode_line_tokens(const std::string& script) {
    std::vector<std::string> tokens;
    std::istringstream lines(script);
    std::string line;
    while (std::getline(lines, line)) {
        const size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == ';') {
            continue;
        }
        const size_t end = line.find_first_of(" \t\r", start);
        std::string token =
            line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        for (char& c : token) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

/**
 * @brief Whether @p script may run while spools lie on the bed (prestonbrown/helixscreen#1730)
 *
 * An allowlist, not a denylist: a macro can home or move without saying G28,
 * so every line's first token must be one that cannot move the toolhead.
 * @p extra_tokens adds the ones a running dry cycle needs (a chamber
 * appliance's own dryer commands), so no vendor name lives here.
 */
inline bool spool_latch_allows(const std::string& script,
                               const std::vector<std::string>& extra_tokens) {
    static constexpr std::array<std::string_view, 25> kAllowed = {
        // heaters
        "M104", "M109", "M140", "M190", "M141", "M191", "SET_HEATER_TEMPERATURE",
        "TURN_OFF_HEATERS", "SET_TEMPERATURE_FAN_TARGET", "SET_IDLE_TIMEOUT",
        // fans and lights
        "M106", "M107", "SET_FAN_SPEED", "SET_PIN", "SET_LED",
        // an emergency stop must always get through; a restart moves nothing
        "M112", "FIRMWARE_RESTART", "RESTART",
        // read-only
        "M105", "M114", "M115", "M117", "M118", "RESPOND", "STATUS"};
    for (const std::string& token : gcode_line_tokens(script)) {
        const bool listed =
            std::find(kAllowed.begin(), kAllowed.end(), token) != kAllowed.end() ||
            std::find(extra_tokens.begin(), extra_tokens.end(), token) != extra_tokens.end();
        if (!listed) {
            return false;
        }
    }
    return true;
}

} // namespace helix

// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_filament_sensors.h"

#include "filament_sensor_manager.h"

#include <algorithm>
#include <cctype>

namespace helix::tool_sensors {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_digit(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

/// Parse the digits at @p pos; returns -1 when there are none. Advances @p pos
/// past them.
int digits_at(const std::string& s, size_t& pos) {
    if (pos >= s.size() || !is_digit(s[pos])) {
        return -1;
    }
    int v = 0;
    while (pos < s.size() && is_digit(s[pos])) {
        v = v * 10 + (s[pos] - '0');
        ++pos;
    }
    return v;
}

/// A token boundary: start of string, or a non-alphanumeric character.
bool boundary_before(const std::string& s, size_t pos) {
    return pos == 0 || !std::isalnum(static_cast<unsigned char>(s[pos - 1]));
}

/// A number directly after @p prefix at a token boundary, e.g. "t0", "tool2",
/// "ex3", "extruder1", "e4". "extruder" (no digit) is tool 0 the way Klipper
/// names it.
int number_after(const std::string& s, const char* prefix, bool bare_means_zero) {
    const std::string p(prefix);
    size_t from = 0;
    while (true) {
        const size_t at = s.find(p, from);
        if (at == std::string::npos) {
            return -1;
        }
        if (boundary_before(s, at)) {
            size_t pos = at + p.size();
            const int n = digits_at(s, pos);
            if (n >= 0) {
                // The digits must end the token: "t10" is tool 10, "t1x" is not a tool.
                if (pos >= s.size() || !std::isalnum(static_cast<unsigned char>(s[pos]))) {
                    return n;
                }
            } else if (bare_means_zero &&
                       (pos >= s.size() || !std::isalnum(static_cast<unsigned char>(s[pos])))) {
                return 0;
            }
        }
        from = at + 1;
    }
}

} // namespace

int tool_for_sensor(const FilamentSensorConfig& sensor) {
    if (sensor.lane >= 0) {
        return sensor.lane;
    }
    const std::string name = lower(sensor.sensor_name);
    // Longest, most specific prefixes first so "extruder1" is not read as
    // "e" + nothing, and "tool0" is not "t" + "ool0".
    for (const char* prefix : {"toolhead", "tool", "extruder", "ex", "t", "e"}) {
        const bool bare_zero = std::string(prefix) == "extruder";
        const int n = number_after(name, prefix, bare_zero);
        if (n >= 0) {
            return n;
        }
    }
    return -1;
}

Kind kind_for_sensor(const FilamentSensorConfig& sensor) {
    const std::string name = lower(sensor.sensor_name);
    for (const char* word : {"entry", "pre", "_in", "inlet", "feed"}) {
        if (name.find(word) != std::string::npos) {
            return Kind::Entry;
        }
    }
    for (const char* word : {"toolhead", "post", "nozzle", "_out", "hotend", "_th"}) {
        if (name.find(word) != std::string::npos) {
            return Kind::Toolhead;
        }
    }
    return sensor.role == FilamentSensorRole::ENTRY ? Kind::Entry : Kind::Toolhead;
}

std::vector<ToolSensor> sensors_for_tool(const std::vector<FilamentSensorConfig>& all,
                                         int tool_number) {
    std::vector<ToolSensor> out;
    for (const auto& s : all) {
        if (tool_for_sensor(s) != tool_number) {
            continue;
        }
        out.push_back({s.klipper_name, kind_for_sensor(s)});
    }
    return out;
}

ToolPips read_pips(int tool_number) {
    ToolPips pips;
    auto& mgr = FilamentSensorManager::instance();
    for (const auto& ts : sensors_for_tool(mgr.get_sensors(), tool_number)) {
        const auto state = mgr.get_sensor_state_by_name(ts.klipper_name);
        std::optional<bool> reading;
        if (state && state->available && state->reported) {
            reading = state->filament_detected;
        }
        if (ts.kind == Kind::Entry) {
            pips.has_entry_sensor = true;
            if (!pips.entry) {
                pips.entry = reading;
            }
        } else {
            pips.has_toolhead_sensor = true;
            if (!pips.toolhead) {
                pips.toolhead = reading;
            }
        }
    }
    return pips;
}

} // namespace helix::tool_sensors

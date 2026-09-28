// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which filament sensors belong to which tool on a tool changer.
//
// A tool changer with a switch before each extruder ("entry") and one at the
// nozzle ("toolhead") reports them as plain filament_switch_sensor objects;
// nothing in Klipper ties a sensor to a tool. This module is the one place
// that convention lives:
//
//   - an explicit `lane` in the sensor's HelixScreen config wins;
//   - otherwise the sensor name carries the tool: "T0_entry", "t0_toolhead",
//     "tool0_pre", "e0_filament", "fd_ex0", "extruder1_runout";
//   - the kind comes from the name too ("entry"/"pre"/"in" vs
//     "toolhead"/"post"/"nozzle"/"out"), falling back to the configured role
//     (Entry -> entry, anything else -> toolhead).
//
// A tool with no matching sensor gets no pip and the path falls back to the
// macros' memory (helix::ToolchangerVars).

#include "filament_sensor_types.h"

#include <optional>
#include <string>
#include <vector>

namespace helix::tool_sensors {

enum class Kind { Entry, Toolhead };

/// One sensor resolved to a tool.
struct ToolSensor {
    std::string klipper_name; ///< "filament_switch_sensor T0_entry"
    Kind kind = Kind::Toolhead;
};

/// The tool number a sensor watches, or -1 when the name carries none.
[[nodiscard]] int tool_for_sensor(const FilamentSensorConfig& sensor);

/// Entry or toolhead, from the name then the role.
[[nodiscard]] Kind kind_for_sensor(const FilamentSensorConfig& sensor);

/// Every sensor that resolves to @p tool_number, from a full sensor list.
[[nodiscard]] std::vector<ToolSensor> sensors_for_tool(const std::vector<FilamentSensorConfig>& all,
                                                       int tool_number);

/// Live readings for one tool's pips. nullopt = no such sensor (or it has not
/// reported), so the caller falls back to memory for that segment.
struct ToolPips {
    std::optional<bool> entry;
    std::optional<bool> toolhead;
    bool has_entry_sensor = false;
    bool has_toolhead_sensor = false;
};

/// Resolve and read @p tool_number's pips through FilamentSensorManager.
[[nodiscard]] ToolPips read_pips(int tool_number);

} // namespace helix::tool_sensors

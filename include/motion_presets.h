// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "axis_move.h"
#include "printer_motion_state.h"

#include <optional>
#include <string_view>

namespace helix {

/// The nine fixed bed positions the Move tab offers as a 3x3 grid. Back is
/// +Y, Front is -Y, Left is -X, Right is +X, all in G-code space.
enum class MotionPreset {
    BackLeft,
    Back,
    BackRight,
    Left,
    Center,
    Right,
    FrontLeft,
    Front,
    FrontRight,
};

/**
 * @brief Bed position a Move-tab preset names, in G-code millimetres
 *
 * X and Y only; a preset never commands Z. On a rectangular bed, edge
 * presets sit 10% of that axis's span in from the edge and Center is the
 * midpoint. On a circular bed (delta), the eight rim presets sit at 90% of
 * the inscribed radius in their direction - a round bed has no corners, so
 * the diagonal presets sit at 45 degrees instead.
 *
 * @param gcode_bounds The machine's kinematic envelope in G-code space
 *        (PrinterState::get_gcode_axis_bounds()).
 * @param circular_bed True on a delta/rotary_delta machine (see
 *        circular_bed_kinematics()).
 * @return nullopt when either axis's bounds are unknown or degenerate; the
 *         caller must skip the move rather than aim at fabricated
 *         coordinates.
 */
std::optional<AxisTarget> motion_preset_target(MotionPreset preset, const AxisBounds& gcode_bounds,
                                               bool circular_bed);

/**
 * @brief Whether a kinematics string names a round-bed machine
 *
 * The value is PrinterDiscovery::kinematics(), read from
 * configfile.config.printer.kinematics because toolhead.kinematics comes
 * back null in the status payload.
 */
constexpr bool circular_bed_kinematics(std::string_view kinematics) {
    return kinematics == "delta" || kinematics == "rotary_delta";
}

} // namespace helix

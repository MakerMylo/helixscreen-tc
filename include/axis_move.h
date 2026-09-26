// SPDX-License-Identifier: GPL-3.0-or-later
// include/axis_move.h
#pragma once

#include <cmath>
#include <optional>
#include <utility>

namespace helix {

/** Relative multi-axis jog delta in mm. */
struct AxisMove {
    double dx = 0.0;
    double dy = 0.0;
    double dz = 0.0;
    // Mixed-magnitude float cancellation can leave ~1e-17 residue in an
    // accumulated delta; treat anything below this as zero so a near-null move
    // (which would serialize in scientific notation) never flushes.
    static constexpr double EPSILON_MM = 1e-6;
    bool any() const {
        return std::abs(dx) > EPSILON_MM || std::abs(dy) > EPSILON_MM || std::abs(dz) > EPSILON_MM;
    }
};

/** Absolute multi-axis target in mm; an unset axis is not commanded. */
struct AxisTarget {
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> z;
    bool any() const {
        return x.has_value() || y.has_value() || z.has_value();
    }
};

} // namespace helix

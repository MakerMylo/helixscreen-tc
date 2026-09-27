// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "motion_presets.h"

#include <iterator>

#include "../catch_amalgamated.hpp"

using helix::AxisBounds;
using helix::circular_bed_kinematics;
using helix::motion_preset_target;
using helix::MotionPreset;

namespace {

AxisBounds known_bounds(float x_min, float x_max, float y_min, float y_max) {
    AxisBounds b;
    b.x_min = x_min;
    b.x_max = x_max;
    b.y_min = y_min;
    b.y_max = y_max;
    b.has_x = true;
    b.has_y = true;
    b.has_z = true;
    return b;
}

struct PresetPoint {
    MotionPreset preset;
    double x;
    double y;
};

/// Assert every preset lands on its exact position and never commands Z.
void check_grid(const PresetPoint* points, size_t count, const AxisBounds& bounds, bool circular) {
    for (size_t i = 0; i < count; ++i) {
        const auto target = motion_preset_target(points[i].preset, bounds, circular);
        CAPTURE(i, circular);
        REQUIRE(target.has_value());
        CHECK(target->x == Catch::Approx(points[i].x).margin(1e-4));
        CHECK(target->y == Catch::Approx(points[i].y).margin(1e-4));
        CHECK_FALSE(target->z.has_value());
    }
}

constexpr double CORNER_REACH_100 = 63.63961030678928; // 90 * sqrt(0.5) on r=100

} // namespace

TEST_CASE("motion presets cover a 235x235 bed", "[motion][presets]") {
    const auto bounds = known_bounds(0, 235, 0, 235);
    // Edge presets sit 10% of the 235 mm span (23.5 mm) in from the edge.
    const PresetPoint grid[] = {
        {MotionPreset::BackLeft, 23.5, 211.5},   {MotionPreset::Back, 117.5, 211.5},
        {MotionPreset::BackRight, 211.5, 211.5}, {MotionPreset::Left, 23.5, 117.5},
        {MotionPreset::Center, 117.5, 117.5},    {MotionPreset::Right, 211.5, 117.5},
        {MotionPreset::FrontLeft, 23.5, 23.5},   {MotionPreset::Front, 117.5, 23.5},
        {MotionPreset::FrontRight, 211.5, 23.5},
    };
    check_grid(grid, std::size(grid), bounds, false);
}

TEST_CASE("motion presets on a centre-origin bed", "[motion][presets]") {
    const auto bounds = known_bounds(-100, 100, -100, 100);
    // 10% of the 200 mm span is 20 mm in from each edge.
    const PresetPoint grid[] = {
        {MotionPreset::BackLeft, -80, 80},   {MotionPreset::Back, 0, 80},
        {MotionPreset::BackRight, 80, 80},   {MotionPreset::Left, -80, 0},
        {MotionPreset::Center, 0, 0},        {MotionPreset::Right, 80, 0},
        {MotionPreset::FrontLeft, -80, -80}, {MotionPreset::Front, 0, -80},
        {MotionPreset::FrontRight, 80, -80},
    };
    check_grid(grid, std::size(grid), bounds, false);
}

TEST_CASE("motion presets on a circular bed sit on the inscribed circle", "[motion][presets]") {
    const auto bounds = known_bounds(-100, 100, -100, 100);
    // Rim presets at 90% of the 100 mm radius; diagonals at 45 degrees, not
    // on the bounding square's corners where the round bed has ended.
    const PresetPoint grid[] = {
        {MotionPreset::BackLeft, -CORNER_REACH_100, CORNER_REACH_100},
        {MotionPreset::Back, 0, 90},
        {MotionPreset::BackRight, CORNER_REACH_100, CORNER_REACH_100},
        {MotionPreset::Left, -90, 0},
        {MotionPreset::Center, 0, 0},
        {MotionPreset::Right, 90, 0},
        {MotionPreset::FrontLeft, -CORNER_REACH_100, -CORNER_REACH_100},
        {MotionPreset::Front, 0, -90},
        {MotionPreset::FrontRight, CORNER_REACH_100, -CORNER_REACH_100},
    };
    check_grid(grid, std::size(grid), bounds, true);

    // The circular flag is the only difference: the same bounds on a
    // rectangular reading put Front at the inset row, not at 90% radius.
    const auto front = motion_preset_target(MotionPreset::Front, bounds, false);
    REQUIRE(front.has_value());
    CHECK(front->y == Catch::Approx(-80.0).margin(1e-4));
}

TEST_CASE("motion presets refuse unknown or degenerate bounds", "[motion][presets]") {
    AxisBounds unknown_x = known_bounds(0, 235, 0, 235);
    unknown_x.has_x = false;
    CHECK_FALSE(motion_preset_target(MotionPreset::Center, unknown_x, false).has_value());
    CHECK_FALSE(motion_preset_target(MotionPreset::BackRight, unknown_x, true).has_value());

    AxisBounds unknown_y = known_bounds(0, 235, 0, 235);
    unknown_y.has_y = false;
    CHECK_FALSE(motion_preset_target(MotionPreset::Front, unknown_y, false).has_value());

    AxisBounds degenerate = known_bounds(100, 100, 0, 235);
    CHECK_FALSE(motion_preset_target(MotionPreset::Center, degenerate, false).has_value());

    AxisBounds inverted = known_bounds(200, 100, 0, 235);
    CHECK_FALSE(motion_preset_target(MotionPreset::Center, inverted, true).has_value());
}

TEST_CASE("circular_bed_kinematics names the round-bed machines", "[motion][presets]") {
    CHECK(circular_bed_kinematics("delta"));
    CHECK(circular_bed_kinematics("rotary_delta"));
    CHECK_FALSE(circular_bed_kinematics("cartesian"));
    CHECK_FALSE(circular_bed_kinematics("corexy"));
    CHECK_FALSE(circular_bed_kinematics(""));
}

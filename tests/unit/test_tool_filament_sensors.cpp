// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure tests for helix::tool_sensors (include/tool_filament_sensors.h): the
// naming convention that ties a plain filament_switch_sensor to a tool
// changer tool, and to the entry or toolhead end of its path.

#include "tool_filament_sensors.h"

#include "../catch_amalgamated.hpp"

using helix::FilamentSensorConfig;
using helix::FilamentSensorRole;
using helix::FilamentSensorType;
using helix::tool_sensors::Kind;
using helix::tool_sensors::kind_for_sensor;
using helix::tool_sensors::sensors_for_tool;
using helix::tool_sensors::tool_for_sensor;

namespace {

FilamentSensorConfig sensor(const std::string& short_name,
                            FilamentSensorRole role = FilamentSensorRole::NONE, int lane = -1) {
    FilamentSensorConfig c("filament_switch_sensor " + short_name, short_name,
                           FilamentSensorType::SWITCH);
    c.role = role;
    c.lane = lane;
    return c;
}

} // namespace

TEST_CASE("tool_for_sensor reads the tool from the sensor name", "[toolchanger][sensors]") {
    CHECK(tool_for_sensor(sensor("T0_entry")) == 0);
    CHECK(tool_for_sensor(sensor("t3_toolhead")) == 3);
    CHECK(tool_for_sensor(sensor("tool2_pre")) == 2);
    CHECK(tool_for_sensor(sensor("fd_ex1")) == 1);
    CHECK(tool_for_sensor(sensor("e4_filament")) == 4);
    CHECK(tool_for_sensor(sensor("extruder1_runout")) == 1);
    CHECK(tool_for_sensor(sensor("extruder_runout")) == 0);
    CHECK(tool_for_sensor(sensor("T10_entry")) == 10);
}

TEST_CASE("tool_for_sensor ignores sensors that name no tool", "[toolchanger][sensors]") {
    CHECK(tool_for_sensor(sensor("runout_sensor")) == -1);
    CHECK(tool_for_sensor(sensor("toolhead")) == -1);
    CHECK(tool_for_sensor(sensor("entry")) == -1);
    CHECK(tool_for_sensor(sensor("t1x_sensor")) == -1);
}

TEST_CASE("an explicit lane wins over the name", "[toolchanger][sensors]") {
    CHECK(tool_for_sensor(sensor("runout_sensor", FilamentSensorRole::RUNOUT, 5)) == 5);
    CHECK(tool_for_sensor(sensor("T0_entry", FilamentSensorRole::NONE, 2)) == 2);
}

TEST_CASE("kind_for_sensor: name words first, then the role", "[toolchanger][sensors]") {
    CHECK(kind_for_sensor(sensor("T0_entry")) == Kind::Entry);
    CHECK(kind_for_sensor(sensor("T0_pre")) == Kind::Entry);
    CHECK(kind_for_sensor(sensor("T0_toolhead")) == Kind::Toolhead);
    CHECK(kind_for_sensor(sensor("T0_nozzle")) == Kind::Toolhead);
    CHECK(kind_for_sensor(sensor("fd_ex0", FilamentSensorRole::ENTRY)) == Kind::Entry);
    CHECK(kind_for_sensor(sensor("fd_ex0", FilamentSensorRole::RUNOUT)) == Kind::Toolhead);
    CHECK(kind_for_sensor(sensor("fd_ex0")) == Kind::Toolhead);
}

TEST_CASE("sensors_for_tool collects both ends of one tool's path", "[toolchanger][sensors]") {
    std::vector<FilamentSensorConfig> all = {
        sensor("T0_entry"),
        sensor("T0_toolhead"),
        sensor("T1_entry"),
        sensor("runout_sensor"),
    };
    auto t0 = sensors_for_tool(all, 0);
    REQUIRE(t0.size() == 2);
    CHECK(t0[0].klipper_name == "filament_switch_sensor T0_entry");
    CHECK(t0[0].kind == Kind::Entry);
    CHECK(t0[1].kind == Kind::Toolhead);
    auto t1 = sensors_for_tool(all, 1);
    REQUIRE(t1.size() == 1);
    CHECK(t1[0].kind == Kind::Entry);
    CHECK(sensors_for_tool(all, 2).empty());
}

// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure tests for helix::ToolchangerVars (include/toolchanger_vars.h): the
// save_variables reader behind the Tools panel's statistics and load memory,
// and the capability provider that puts save_variables on the subscription.

#include "printer_discovery.h"
#include "toolchanger_vars.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using json = nlohmann::json;

namespace {

helix::PrinterDiscovery tool_changer_with(const std::vector<std::string>& extra) {
    helix::PrinterDiscovery hw;
    json objects =
        json::array({"gcode_move", "toolchanger", "tool T0", "tool T1", "extruder", "extruder1"});
    for (const auto& o : extra) {
        objects.push_back(o);
    }
    hw.parse_objects(objects);
    std::vector<std::string> names;
    for (const auto& o : objects) {
        names.push_back(o.get<std::string>());
    }
    hw.set_printer_objects(names);
    return hw;
}

json frame(json variables) {
    return {{"save_variables", {{"variables", std::move(variables)}}}};
}

} // namespace

TEST_CASE("toolchanger_vars subscribes save_variables only on a tool changer that has it",
          "[toolchanger][vars]") {
    SECTION("tool changer with save_variables") {
        auto hw = tool_changer_with({"save_variables"});
        REQUIRE(helix::toolchanger_vars::required_status_objects(hw) ==
                std::vector<std::string>{"save_variables"});
    }
    SECTION("tool changer without the object") {
        auto hw = tool_changer_with({});
        REQUIRE(helix::toolchanger_vars::required_status_objects(hw).empty());
    }
    SECTION("single-tool printer with save_variables") {
        helix::PrinterDiscovery hw;
        json objects = json::array({"gcode_move", "extruder", "save_variables"});
        hw.parse_objects(objects);
        hw.set_printer_objects({"gcode_move", "extruder", "save_variables"});
        REQUIRE(helix::toolchanger_vars::required_status_objects(hw).empty());
    }
}

TEST_CASE("ToolchangerVars parses tc_stats, tc_loaded and tc_last_tool", "[toolchanger][vars]") {
    auto& vars = helix::ToolchangerVars::instance();
    vars.reset();
    REQUIRE_FALSE(vars.has_data());
    REQUIRE_FALSE(vars.stats_for(0).has_value());
    REQUIRE(vars.loaded_for(0) == -1);

    vars.update_from_status(frame({
        {"tc_stats",
         {{"T0",
           {{"ups", 12}, {"downs", 11}, {"ups_failed", 1}, {"downs_failed", 0}, {"jiggled", 2}}},
          {"T5", {{"ups", 3}, {"downs", 3}}}}},
        {"tc_loaded", {{"T0", 1}, {"T1", 0}, {"T2", true}}},
        {"tc_last_tool", 5},
    }));

    REQUIRE(vars.has_data());
    auto t0 = vars.stats_for(0);
    REQUIRE(t0.has_value());
    CHECK(t0->ups == 12);
    CHECK(t0->downs == 11);
    CHECK(t0->ups_failed == 1);
    CHECK(t0->jiggled == 2);
    // 23 changes, 1 failed: 22/23 = 95%
    CHECK(t0->success_rate_pct() == 95);
    auto t5 = vars.stats_for("T5");
    REQUIRE(t5.has_value());
    CHECK(t5->ups_failed == 0);
    CHECK(t5->success_rate_pct() == 100);
    CHECK_FALSE(vars.stats_for(3).has_value());

    CHECK(vars.loaded_for(0) == 1);
    CHECK(vars.loaded_for(1) == 0);
    CHECK(vars.loaded_for(2) == 1);
    CHECK(vars.loaded_for(3) == -1);
    CHECK(vars.last_tool() == 5);
}

TEST_CASE("ToolchangerVars treats a frame without its keys as no news", "[toolchanger][vars]") {
    auto& vars = helix::ToolchangerVars::instance();
    vars.reset();
    vars.update_from_status(frame({{"tc_loaded", {{"T1", 1}}}}));
    REQUIRE(vars.loaded_for(1) == 1);

    // Moonraker republishes only what changed: an unrelated variable must not
    // clear what we hold.
    vars.update_from_status(frame({{"was_interrupted", false}}));
    CHECK(vars.loaded_for(1) == 1);

    // No save_variables at all: ignored.
    vars.update_from_status(json{{"toolhead", {{"position", json::array({0, 0, 0, 0})}}}});
    CHECK(vars.loaded_for(1) == 1);

    // A cleared stats dict is real news (TOOLCHANGER_STATS_RESET).
    vars.update_from_status(frame({{"tc_stats", {{"T1", {{"ups", 1}}}}}}));
    REQUIRE(vars.stats_for(1).has_value());
    vars.update_from_status(frame({{"tc_stats", json::object()}}));
    CHECK_FALSE(vars.stats_for(1).has_value());
}

TEST_CASE("ToolChangeStats rate is unknown with no changes", "[toolchanger][vars]") {
    helix::ToolChangeStats s;
    CHECK(s.success_rate_pct() == -1);
    s.ups = 4;
    s.downs = 4;
    s.downs_failed = 2;
    CHECK(s.success_rate_pct() == 75);
}

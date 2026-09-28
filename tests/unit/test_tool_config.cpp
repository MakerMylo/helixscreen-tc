// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests for helix::ToolConfig (include/tool_config.h): the tool count override
// and the toolhead colours behind the Tools panel and its settings page.
// Config has no path here, so save() is a no-op and everything stays in
// memory.

#include "config.h"
#include "tool_config.h"

#include "../catch_amalgamated.hpp"

using helix::ToolConfig;

namespace {

struct Fresh {
    Fresh() {
        auto& c = ToolConfig::instance();
        c.set_tool_count(0);
        for (int i = 0; i < ToolConfig::kMaxTools; ++i) {
            c.reset_color(i);
        }
    }
};

} // namespace

TEST_CASE("ToolConfig default palette starts with the rainbow", "[toolchanger][config]") {
    CHECK(std::string(ToolConfig::default_color_name(0)) == "Red");
    CHECK(std::string(ToolConfig::default_color_name(1)) == "Orange");
    CHECK(std::string(ToolConfig::default_color_name(2)) == "Yellow");
    CHECK(std::string(ToolConfig::default_color_name(3)) == "Green");
    CHECK(std::string(ToolConfig::default_color_name(4)) == "Blue");
    CHECK(std::string(ToolConfig::default_color_name(5)) == "Purple");
    // Past the palette it cycles rather than running out.
    CHECK(ToolConfig::default_color(12) == ToolConfig::default_color(0));
    CHECK(ToolConfig::default_color(-1) == ToolConfig::default_color(0));
}

TEST_CASE("ToolConfig count override", "[toolchanger][config]") {
    Fresh fresh;
    auto& c = ToolConfig::instance();
    CHECK(c.tool_count() == 0);
    CHECK(c.effective_tool_count(6) == 6);
    c.set_tool_count(4);
    CHECK(c.effective_tool_count(6) == 4);
    c.set_tool_count(8);
    CHECK(c.effective_tool_count(6) == 8);
    // Out of range is ignored, not clamped.
    c.set_tool_count(ToolConfig::kMaxTools + 1);
    CHECK(c.tool_count() == 8);
    c.set_tool_count(-3);
    CHECK(c.tool_count() == 8);
    c.set_tool_count(0);
    CHECK(c.effective_tool_count(6) == 6);
}

TEST_CASE("ToolConfig colours: custom, default, reset", "[toolchanger][config]") {
    Fresh fresh;
    auto& c = ToolConfig::instance();
    CHECK_FALSE(c.has_custom_color(2));
    CHECK(c.color(2) == ToolConfig::default_color(2));
    CHECK(c.color_name(2) == "Yellow");

    c.set_color(2, 0x123456);
    CHECK(c.has_custom_color(2));
    CHECK(c.color(2) == 0x123456);
    CHECK_FALSE(c.color_name(2).empty());
    // Neighbours untouched.
    CHECK_FALSE(c.has_custom_color(1));
    CHECK_FALSE(c.has_custom_color(3));

    c.reset_color(2);
    CHECK_FALSE(c.has_custom_color(2));
    CHECK(c.color(2) == ToolConfig::default_color(2));

    // Out-of-range tools are ignored.
    c.set_color(ToolConfig::kMaxTools, 0xFFFFFF);
    CHECK_FALSE(c.has_custom_color(ToolConfig::kMaxTools));
    c.set_color(-1, 0xFFFFFF);
    CHECK(c.color(0) == ToolConfig::default_color(0));
}

TEST_CASE("ToolConfig round-trips through Config", "[toolchanger][config]") {
    Fresh fresh;
    auto& c = ToolConfig::instance();
    c.set_tool_count(6);
    c.set_color(0, 0xAA0000);
    c.set_color(5, 0x0000AA);

    // What went into Config is what a restart would read back.
    Config* cfg = Config::get_instance();
    REQUIRE(cfg != nullptr);
    CHECK(cfg->get<int>(cfg->df() + "toolchanger/tool_count", -1) == 6);
    const auto stored =
        cfg->get<std::vector<std::string>>(cfg->df() + "toolchanger/tool_colors", {});
    REQUIRE(stored.size() == 6);
    CHECK(stored[0] == "#AA0000");
    CHECK(stored[1].empty());
    CHECK(stored[5] == "#0000AA");

    c.reset();
    CHECK(c.tool_count() == 0);
    CHECK_FALSE(c.has_custom_color(0));
    c.load();
    CHECK(c.tool_count() == 6);
    CHECK(c.color(0) == 0xAA0000);
    CHECK(c.color(5) == 0x0000AA);
    CHECK_FALSE(c.has_custom_color(1));

    // A garbage entry reads as default rather than breaking the palette.
    cfg->set<std::vector<std::string>>(cfg->df() + "toolchanger/tool_colors",
                                       std::vector<std::string>{"#AA0000", "not-a-colour"});
    c.load();
    CHECK(c.color(0) == 0xAA0000);
    CHECK_FALSE(c.has_custom_color(1));
    CHECK(c.color(1) == ToolConfig::default_color(1));
}

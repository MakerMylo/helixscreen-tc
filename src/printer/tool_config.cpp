// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tool_config.h"

#include "ui_color_picker.h" // get_color_name_from_hex

#include "config.h"
#include "printer_cache_registry.h"
#include "state/subject_macros.h"
#include "static_subject_registry.h"

#include <spdlog/spdlog.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace helix {

namespace {

constexpr const char* kCountKey = "toolchanger/tool_count";
constexpr const char* kColorsKey = "toolchanger/tool_colors";

struct NamedColor {
    uint32_t rgb;
    const char* name;
};

/// Rainbow first (the order a six-tool StealthChanger is usually built in),
/// then hues that stay distinct from those six.
constexpr std::array<NamedColor, 12> kPalette = {{
    {0xE53935, "Red"},
    {0xFB8C00, "Orange"},
    {0xFDD835, "Yellow"},
    {0x43A047, "Green"},
    {0x1E88E5, "Blue"},
    {0x8E24AA, "Purple"},
    {0xEC407A, "Pink"},
    {0x00897B, "Teal"},
    {0x6D4C41, "Brown"},
    {0x00ACC1, "Cyan"},
    {0xC0CA33, "Lime"},
    {0x757575, "Grey"},
}};

bool parse_hex(const std::string& s, uint32_t& out) {
    if (s.empty()) {
        return false;
    }
    const char* p = s.c_str();
    if (*p == '#') {
        ++p;
    }
    if (std::strlen(p) != 6) {
        return false;
    }
    char* end = nullptr;
    const unsigned long v = std::strtoul(p, &end, 16);
    if (!end || *end != '\0') {
        return false;
    }
    out = static_cast<uint32_t>(v & 0xFFFFFF);
    return true;
}

std::string to_hex(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFF);
    return buf;
}

} // namespace

ToolConfig& ToolConfig::instance() {
    static ToolConfig inst;
    return inst;
}

void ToolConfig::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        return;
    }
    INIT_SUBJECT_INT(tool_config_version, 0, subjects_, register_xml);
    subjects_initialized_ = true;
    // Per-printer values: re-read when the active printer changes.
    PrinterCacheRegistry::instance().register_invalidator("ToolConfig",
                                                          []() { ToolConfig::instance().load(); });
    StaticSubjectRegistry::instance().register_deinit(
        "ToolConfig", []() { ToolConfig::instance().deinit_subjects(); });
}

void ToolConfig::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void ToolConfig::reset() {
    tool_count_ = 0;
    colors_.clear();
}

void ToolConfig::load() {
    reset();
    Config* cfg = Config::get_instance();
    if (!cfg) {
        return;
    }
    const std::string base = cfg->df();
    tool_count_ = cfg->get<int>(base + kCountKey, 0);
    if (tool_count_ < 0 || tool_count_ > kMaxTools) {
        spdlog::warn("[ToolConfig] tool_count {} out of range, using auto", tool_count_);
        tool_count_ = 0;
    }
    colors_ = cfg->get<std::vector<std::string>>(base + kColorsKey, {});
    if (colors_.size() > static_cast<size_t>(kMaxTools)) {
        colors_.resize(static_cast<size_t>(kMaxTools));
    }
    // A stored value that does not parse is treated as "default" rather than
    // left to surprise the drawing code later.
    for (auto& c : colors_) {
        uint32_t rgb = 0;
        if (!c.empty() && !parse_hex(c, rgb)) {
            spdlog::warn("[ToolConfig] ignoring unparseable tool colour '{}'", c);
            c.clear();
        }
    }
    bump();
}

uint32_t ToolConfig::default_color(int tool) {
    if (tool < 0) {
        tool = 0;
    }
    return kPalette[static_cast<size_t>(tool) % kPalette.size()].rgb;
}

const char* ToolConfig::default_color_name(int tool) {
    if (tool < 0) {
        tool = 0;
    }
    return kPalette[static_cast<size_t>(tool) % kPalette.size()].name;
}

bool ToolConfig::has_custom_color(int tool) const {
    if (tool < 0 || static_cast<size_t>(tool) >= colors_.size()) {
        return false;
    }
    uint32_t rgb = 0;
    return parse_hex(colors_[static_cast<size_t>(tool)], rgb);
}

uint32_t ToolConfig::color(int tool) const {
    if (tool >= 0 && static_cast<size_t>(tool) < colors_.size()) {
        uint32_t rgb = 0;
        if (parse_hex(colors_[static_cast<size_t>(tool)], rgb)) {
            return rgb;
        }
    }
    return default_color(tool);
}

std::string ToolConfig::color_name(int tool) const {
    if (!has_custom_color(tool)) {
        return default_color_name(tool);
    }
    return get_color_name_from_hex(color(tool));
}

void ToolConfig::set_tool_count(int count) {
    if (count < 0 || count > kMaxTools) {
        return;
    }
    if (count == tool_count_) {
        return;
    }
    tool_count_ = count;
    persist();
}

void ToolConfig::set_color(int tool, uint32_t rgb) {
    if (tool < 0 || tool >= kMaxTools) {
        return;
    }
    const auto idx = static_cast<size_t>(tool);
    if (colors_.size() <= idx) {
        colors_.resize(idx + 1);
    }
    const std::string hex = to_hex(rgb);
    if (colors_[idx] == hex) {
        return;
    }
    colors_[idx] = hex;
    persist();
}

void ToolConfig::reset_color(int tool) {
    if (tool < 0 || static_cast<size_t>(tool) >= colors_.size() ||
        colors_[static_cast<size_t>(tool)].empty()) {
        return;
    }
    colors_[static_cast<size_t>(tool)].clear();
    // Drop trailing defaults so the stored list stays as short as the edits.
    while (!colors_.empty() && colors_.back().empty()) {
        colors_.pop_back();
    }
    persist();
}

void ToolConfig::persist() {
    if (Config* cfg = Config::get_instance()) {
        const std::string base = cfg->df();
        cfg->set<int>(base + kCountKey, tool_count_);
        cfg->set<std::vector<std::string>>(base + kColorsKey, colors_);
        cfg->save();
    }
    bump();
}

void ToolConfig::bump() {
    if (subjects_initialized_) {
        lv_subject_set_int(&tool_config_version_, lv_subject_get_int(&tool_config_version_) + 1);
    }
}

} // namespace helix

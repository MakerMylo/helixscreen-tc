// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "toolchanger_vars.h"

#include "display_numbering.h"
#include "json_utils.h"
#include "printer_discovery.h"
#include "state/subject_macros.h"
#include "static_subject_registry.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace helix {

namespace {

constexpr const char* kObject = "save_variables";
constexpr const char* kStatsVar = "tc_stats";
constexpr const char* kLoadedVar = "tc_loaded";
constexpr const char* kLastToolVar = "tc_last_tool";

/// The tool name a number maps to in the variables: klipper-toolchanger's
/// default "T<n>", which is also what the macros key their dicts by.
std::string key_for(int tool_number) {
    return helix::ui::tool_label(tool_number);
}

int int_field(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end()) {
        return 0;
    }
    if (it->is_number_integer()) {
        return it->get<int>();
    }
    if (it->is_number_float()) {
        return static_cast<int>(it->get<double>());
    }
    if (it->is_boolean()) {
        return it->get<bool>() ? 1 : 0;
    }
    return 0;
}

} // namespace

namespace toolchanger_vars {

std::vector<std::string> required_status_objects(const PrinterDiscovery& hw) {
    if (!hw.has_tool_changer()) {
        return {};
    }
    const auto& objects = hw.printer_objects();
    if (std::find(objects.begin(), objects.end(), kObject) == objects.end()) {
        return {};
    }
    return {std::string(kObject)};
}

} // namespace toolchanger_vars

ToolchangerVars& ToolchangerVars::instance() {
    static ToolchangerVars inst;
    return inst;
}

void ToolchangerVars::init_subjects(bool register_xml) {
    if (subjects_initialized_) {
        return;
    }
    INIT_SUBJECT_INT(tc_vars_version, 0, subjects_, register_xml);
    subjects_initialized_ = true;
    StaticSubjectRegistry::instance().register_deinit(
        "ToolchangerVars", []() { ToolchangerVars::instance().deinit_subjects(); });
}

void ToolchangerVars::deinit_subjects() {
    if (!subjects_initialized_) {
        return;
    }
    reset();
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void ToolchangerVars::reset() {
    stats_.clear();
    loaded_.clear();
    last_tool_ = -1;
    has_data_ = false;
}

void ToolchangerVars::update_from_status(const nlohmann::json& status) {
    if (!status.is_object()) {
        return;
    }
    auto sv_it = status.find(kObject);
    if (sv_it == status.end() || !sv_it->is_object()) {
        return;
    }
    auto vars_it = sv_it->find("variables");
    if (vars_it == sv_it->end() || !vars_it->is_object()) {
        return;
    }
    const nlohmann::json& vars = *vars_it;
    bool changed = false;

    if (auto it = vars.find(kStatsVar); it != vars.end() && it->is_object()) {
        std::map<std::string, ToolChangeStats> next;
        for (auto tool = it->begin(); tool != it->end(); ++tool) {
            if (!tool.value().is_object()) {
                continue;
            }
            ToolChangeStats s;
            s.ups = int_field(tool.value(), "ups");
            s.downs = int_field(tool.value(), "downs");
            s.ups_failed = int_field(tool.value(), "ups_failed");
            s.downs_failed = int_field(tool.value(), "downs_failed");
            s.jiggled = int_field(tool.value(), "jiggled");
            next[tool.key()] = s;
        }
        // A cleared dict (TOOLCHANGER_STATS_RESET) is real news too; an
        // identical one (a mock that republishes every frame) is not.
        if (!has_data_ || next != stats_) {
            stats_ = std::move(next);
            changed = true;
        }
        has_data_ = true;
    }

    if (auto it = vars.find(kLoadedVar); it != vars.end() && it->is_object()) {
        std::map<std::string, int> next;
        for (auto tool = it->begin(); tool != it->end(); ++tool) {
            int v = -1;
            if (tool.value().is_boolean()) {
                v = tool.value().get<bool>() ? 1 : 0;
            } else if (tool.value().is_number()) {
                v = tool.value().get<double>() != 0.0 ? 1 : 0;
            }
            if (v >= 0) {
                next[tool.key()] = v;
            }
        }
        if (!has_data_ || next != loaded_) {
            loaded_ = std::move(next);
            changed = true;
        }
        has_data_ = true;
    }

    if (auto it = vars.find(kLastToolVar); it != vars.end() && it->is_number()) {
        const int v = static_cast<int>(it->get<double>());
        if (v != last_tool_) {
            last_tool_ = v;
            changed = true;
        }
    }

    if (changed && subjects_initialized_) {
        lv_subject_set_int(&tc_vars_version_, lv_subject_get_int(&tc_vars_version_) + 1);
        spdlog::debug("[ToolchangerVars] updated: {} tools with stats, {} with load memory, "
                      "last tool {}",
                      stats_.size(), loaded_.size(), last_tool_);
    }
}

std::optional<ToolChangeStats> ToolchangerVars::stats_for(const std::string& tool_name) const {
    auto it = stats_.find(tool_name);
    if (it == stats_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<ToolChangeStats> ToolchangerVars::stats_for(int tool_number) const {
    return stats_for(key_for(tool_number));
}

int ToolchangerVars::loaded_for(const std::string& tool_name) const {
    auto it = loaded_.find(tool_name);
    return it == loaded_.end() ? -1 : it->second;
}

int ToolchangerVars::loaded_for(int tool_number) const {
    return loaded_for(key_for(tool_number));
}

} // namespace helix

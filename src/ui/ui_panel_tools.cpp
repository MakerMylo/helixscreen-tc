// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_panel_tools.h"

#include "ui_callback_helpers.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_overlay_tool_actions.h"

#include "ams_state.h"
#include "app_globals.h"
#include "display_numbering.h"
#include "exception_policy.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "static_panel_registry.h"
#include "tool_config.h"
#include "tool_state.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace helix::ui {

namespace {

std::unique_ptr<ToolsPanel> g_tools_panel;

} // namespace

ToolsPanel& get_global_tools_panel() {
    if (!g_tools_panel) {
        spdlog::error("[Tools Panel] get_global_tools_panel() called before initialization!");
        helix::throw_or_abort(std::runtime_error("ToolsPanel not initialized"));
    }
    return *g_tools_panel;
}

void init_global_tools_panel(helix::PrinterState& printer_state, IMoonrakerAPI* api) {
    g_tools_panel = std::make_unique<ToolsPanel>(printer_state, api);
    StaticPanelRegistry::instance().register_destroy("ToolsPanel", []() { g_tools_panel.reset(); });
}

// ============================================================================
// LIFECYCLE
// ============================================================================

ToolsPanel::ToolsPanel(helix::PrinterState& printer_state, IMoonrakerAPI* api)
    : PanelBase(printer_state, api) {}

ToolsPanel::~ToolsPanel() {
    watches_.clear();
    tools_observer_.reset();
    slots_observer_.reset();
    extruders_observer_.reset();
    config_observer_.reset();
    deinit_subjects_base(subjects_);
}

void ToolsPanel::init_subjects() {
    init_subjects_guarded([this]() {
        // Pools first, then the count they are sized for (see refresh_columns).
        UI_MANAGED_SUBJECT_INT(count_, 0, "tools_count", subjects_);
        register_xml_callbacks({
            {"on_tools_select", on_select_clicked},
            {"on_tools_opts", on_opts_clicked},
        });
    });
}

void ToolsPanel::setup(lv_obj_t* panel, lv_obj_t* parent_screen) {
    PanelBase::setup(panel, parent_screen);
    if (!panel_) {
        spdlog::error("[{}] NULL panel", get_name());
        return;
    }
    ui_alive_ = true;

    auto& tools = helix::ToolState::instance();
    tools_observer_ = observe_int_sync<ToolsPanel>(
        tools.get_tools_version_subject(), this,
        [](ToolsPanel* self, int) {
            self->refresh_columns();
            self->bind_temperatures();
        },
        tools.get_subjects_lifetime());
    auto& ams = AmsState::instance();
    slots_observer_ = observe_int_sync<ToolsPanel>(
        ams.get_slots_version_subject(), this,
        [](ToolsPanel* self, int) { self->refresh_columns(); }, ams.get_subjects_lifetime());
    extruders_observer_ = observe_int_sync<ToolsPanel>(
        printer_state_.get_extruder_version_subject(), this,
        [](ToolsPanel* self, int) { self->bind_temperatures(); },
        printer_state_.get_subjects_lifetime());

    auto& config = helix::ToolConfig::instance();
    config_observer_ = observe_int_sync<ToolsPanel>(
        config.get_version_subject(), this, [](ToolsPanel* self, int) { self->refresh_columns(); },
        config.get_subjects_lifetime());

    refresh_columns();
    bind_temperatures();
    spdlog::debug("[{}] Setup complete", get_name());
}

void ToolsPanel::on_activate() {
    refresh_columns();
    refresh_temps();
}

void ToolsPanel::on_deactivating(DeactivateReason reason) {
    if (reason == DeactivateReason::Rebuild || reason == DeactivateReason::Shutdown) {
        // The widgets go away: the pooled subjects the <repeat> bound to must
        // be reclaimed before the next build, and the count zeroed so a
        // rebuild starts empty.
        ui_alive_ = false;
        names_.reclaim();
        materials_.reclaim();
        temps_.reclaim();
        if (subjects_initialized_) {
            lv_subject_set_int(&count_, 0);
        }
    }
}

// ============================================================================
// COLUMNS
// ============================================================================

void ToolsPanel::refresh_columns() {
    if (!subjects_initialized_ || !ui_alive_) {
        return;
    }
    const auto& tools = helix::ToolState::instance().tools();
    // The user's count when set (Settings > Tool Changer), else what Klipper
    // reported. Columns past the reported tools show as absent.
    const auto count = static_cast<size_t>(
        helix::ToolConfig::instance().effective_tool_count(static_cast<int>(tools.size())));
    names_.ensure_size(count);
    materials_.ensure_size(count);
    temps_.ensure_size(count);
    for (size_t i = 0; i < count; ++i) {
        refresh_column(static_cast<int>(i));
    }
    lv_subject_set_int(&count_, static_cast<int>(count));
}

void ToolsPanel::refresh_column(int tool) {
    const auto& tools = helix::ToolState::instance().tools();
    const auto idx = static_cast<size_t>(tool);
    if (idx >= tools.size()) {
        // Configured but not reported by the printer.
        names_.set_string(idx, tool_label(tool));
        materials_.set_string(idx, "--");
        return;
    }
    names_.set_string(idx, tools[idx].name);

    std::string material = "--";
    if (auto* backend = AmsState::instance().get_backend()) {
        const SlotInfo info = backend->get_slot_info(tool);
        if (info.slot_index >= 0 && !info.material.empty()) {
            material = info.material;
        }
    }
    materials_.set_string(idx, material);
}

// ============================================================================
// TEMPERATURES
// ============================================================================

void ToolsPanel::bind_temperatures() {
    watches_.clear();
    const auto& tools = helix::ToolState::instance().tools();
    watches_.resize(tools.size());
    for (size_t i = 0; i < tools.size(); ++i) {
        auto& w = watches_[i];
        w.extruder = tools[i].extruder_name.value_or("");
        if (w.extruder.empty()) {
            continue;
        }
        SubjectLifetime temp_lt;
        SubjectLifetime target_lt;
        auto* temp = printer_state_.get_extruder_temp_subject(w.extruder, temp_lt);
        auto* target = printer_state_.get_extruder_target_subject(w.extruder, target_lt);
        if (temp) {
            w.temp_decideg = lv_subject_get_int(temp);
            // Captures the extruder name, not the index: a rebind between
            // enqueue and apply must not land on another tool's row.
            const std::string name = w.extruder;
            w.temp = observe_int_sync<ToolsPanel>(
                temp, this,
                [name](ToolsPanel* self, int v) { self->on_extruder_temp(name, v, false); },
                temp_lt);
        }
        if (target) {
            w.target_decideg = lv_subject_get_int(target);
            const std::string name = w.extruder;
            w.target = observe_int_sync<ToolsPanel>(
                target, this,
                [name](ToolsPanel* self, int v) { self->on_extruder_temp(name, v, true); },
                target_lt);
        }
    }
    refresh_temps();
}

void ToolsPanel::on_extruder_temp(const std::string& extruder, int decideg, bool is_target) {
    for (auto& w : watches_) {
        if (w.extruder != extruder) {
            continue;
        }
        if (is_target) {
            w.target_decideg = decideg;
        } else {
            w.temp_decideg = decideg;
        }
    }
    refresh_temps();
}

void ToolsPanel::refresh_temps() {
    if (!subjects_initialized_ || !ui_alive_) {
        return;
    }
    // One entry per reported tool; configured-only columns have no extruder.
    for (size_t i = 0; i < watches_.size() && i < temps_.size(); ++i) {
        const auto& w = watches_[i];
        std::string text;
        if (w.extruder.empty()) {
            text = "--";
        } else if (w.target_decideg > 0) {
            text = fmt::format("{} / {}°", w.temp_decideg / 10, w.target_decideg / 10);
        } else {
            text = fmt::format("{}°", w.temp_decideg / 10);
        }
        temps_.set_string(i, text);
    }
}

// ============================================================================
// ACTIONS
// ============================================================================

void ToolsPanel::select_tool(int tool) {
    auto* api = get_moonraker_api();
    if (!api) {
        NOTIFY_WARNING(lv_tr("Not connected"));
        return;
    }
    const auto& tools = helix::ToolState::instance().tools();
    if (tool < 0 || tool >= static_cast<int>(tools.size())) {
        return;
    }
    if (tools[static_cast<size_t>(tool)].active) {
        NOTIFY_INFO(lv_tr("{} is already on the carriage"), tools[static_cast<size_t>(tool)].name);
        return;
    }
    helix::ToolState::instance().request_tool_change(
        tool, api, nullptr, lifetime_.bg_cb("ToolsPanel::select_err", [](const std::string& err) {
            NOTIFY_ERROR("{}", err);
        }));
}

void ToolsPanel::open_actions(int tool) {
    if (!parent_screen_) {
        return;
    }
    get_global_tool_actions_overlay().show_for_tool(parent_screen_, tool);
}

int ToolsPanel::tool_from_event(lv_event_t* e) {
    lv_obj_t* target = lv_event_get_current_target_obj(e);
    const char* name = target ? lv_obj_get_name(target) : nullptr;
    if (!name) {
        return -1;
    }
    // "tools_select_3" / "tools_opts_3": the index is what follows the last '_'.
    const char* us = std::strrchr(name, '_');
    if (!us || !us[1]) {
        return -1;
    }
    return std::atoi(us + 1);
}

void ToolsPanel::on_select_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolsPanel] select");
    const int tool = tool_from_event(e);
    if (tool >= 0) {
        get_global_tools_panel().select_tool(tool);
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ToolsPanel::on_opts_clicked(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolsPanel] options");
    const int tool = tool_from_event(e);
    if (tool >= 0) {
        get_global_tools_panel().open_actions(tool);
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui

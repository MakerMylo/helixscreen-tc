// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_overlay_tool_actions.h"

#include "ui_ams_edit_overlay.h"
#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"

#include "ams_state.h"
#include "app_globals.h"
#include "display_numbering.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "static_panel_registry.h"
#include "tool_state.h"
#include "toolchanger_vars.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace helix::ui {

namespace {

std::unique_ptr<ToolActionsOverlay> g_overlay;

} // namespace

ToolActionsOverlay& get_global_tool_actions_overlay() {
    if (!g_overlay) {
        g_overlay = std::make_unique<ToolActionsOverlay>();
        StaticPanelRegistry::instance().register_destroy("ToolActionsOverlay",
                                                         []() { g_overlay.reset(); });
    }
    return *g_overlay;
}

// ============================================================================
// LIFECYCLE
// ============================================================================

ToolActionsOverlay::ToolActionsOverlay() = default;

ToolActionsOverlay::~ToolActionsOverlay() {
    tools_observer_.reset();
    vars_observer_.reset();
    slots_observer_.reset();
    subjects_.deinit_all();
    subjects_initialized_ = false;
}

void ToolActionsOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }
    UI_MANAGED_SUBJECT_INT(index_, 0, "tool_act_index", subjects_);
    UI_MANAGED_SUBJECT_STRING(title_, title_buf_, "", "tool_act_title", subjects_);
    UI_MANAGED_SUBJECT_STRING(stats_, stats_buf_, "", "tool_act_stats", subjects_);
    UI_MANAGED_SUBJECT_STRING(mount_label_, mount_buf_, "", "tool_act_mount_label", subjects_);
    UI_MANAGED_SUBJECT_STRING(material_, material_buf_, "", "tool_act_material", subjects_);
    UI_MANAGED_SUBJECT_STRING(loaded_, loaded_buf_, "", "tool_act_loaded", subjects_);
    UI_MANAGED_SUBJECT_INT(busy_subject_, 0, "tool_act_busy", subjects_);
    UI_MANAGED_SUBJECT_STRING(status_, status_buf_, "", "tool_act_status", subjects_);

    static const std::pair<const char*, lv_event_cb_t> callbacks[] = {
        {"on_tool_act_mount", on_mount_clicked},     {"on_tool_act_load", on_load_clicked},
        {"on_tool_act_unload", on_unload_clicked},   {"on_tool_act_extrude", on_extrude_clicked},
        {"on_tool_act_retract", on_retract_clicked}, {"on_tool_act_change", on_change_clicked},
    };
    for (const auto& [name, cb] : callbacks) {
        lv_xml_register_event_cb(nullptr, name, cb);
    }
    subjects_initialized_ = true;
}

lv_obj_t* ToolActionsOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        return overlay_root_;
    }
    parent_screen_ = parent;
    if (!create_overlay_from_xml(parent, "tool_actions_overlay")) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }
    refresh();
    return overlay_root_;
}

void ToolActionsOverlay::show_for_tool(lv_obj_t* parent, int tool) {
    parent_screen_ = parent;
    if (!subjects_initialized_) {
        init_subjects();
    }
    tool_ = tool;
    lv_subject_set_int(&index_, tool);
    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }
    if (!overlay_root_) {
        return;
    }
    refresh();
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);
    NavigationManager::instance().push_overlay(overlay_root_);
}

void ToolActionsOverlay::on_ui_destroyed() {
    // Nothing pooled; the subjects are fixed and outlive the widgets.
}

void ToolActionsOverlay::on_activate() {
    OverlayBase::on_activate();
    auto& tools = helix::ToolState::instance();
    tools_observer_ = observe_int_sync<ToolActionsOverlay>(
        tools.get_tools_version_subject(), this,
        [](ToolActionsOverlay* self, int) { self->refresh(); }, tools.get_subjects_lifetime());
    auto& vars = helix::ToolchangerVars::instance();
    vars_observer_ = observe_int_sync<ToolActionsOverlay>(
        vars.get_version_subject(), this, [](ToolActionsOverlay* self, int) { self->refresh(); },
        vars.get_subjects_lifetime());
    auto& ams = AmsState::instance();
    slots_observer_ = observe_int_sync<ToolActionsOverlay>(
        ams.get_slots_version_subject(), this,
        [](ToolActionsOverlay* self, int) { self->refresh(); }, ams.get_subjects_lifetime());
    refresh();
}

void ToolActionsOverlay::on_deactivating(DeactivateReason reason) {
    // An action in flight keeps going on the printer; its completion rides
    // object_lifetime_ (see send()), so the busy flag clears when it answers.
    spdlog::debug("[{}] on_deactivating({})", get_name(), deactivate_reason_name(reason));
    tools_observer_.reset();
    vars_observer_.reset();
    slots_observer_.reset();
}

void ToolActionsOverlay::cleanup() {
    object_lifetime_.invalidate();
    tools_observer_.reset();
    vars_observer_.reset();
    slots_observer_.reset();
    if (overlay_root_) {
        NavigationManager::instance().unregister_overlay_instance(overlay_root_);
    }
    OverlayBase::cleanup();
    parent_screen_ = nullptr;
}

// ============================================================================
// CONTENT
// ============================================================================

void ToolActionsOverlay::refresh() {
    if (!subjects_initialized_) {
        return;
    }
    auto& tools = helix::ToolState::instance();
    const auto& list = tools.tools();
    const bool exists = tool_ >= 0 && tool_ < static_cast<int>(list.size());
    const std::string name = exists ? list[static_cast<size_t>(tool_)].name : tool_label(tool_);
    const bool mounted = exists && list[static_cast<size_t>(tool_)].active;

    lv_subject_copy_string(&title_, name.c_str());
    lv_subject_copy_string(&mount_label_, mounted ? lv_tr("Dock") : lv_tr("Pick up"));

    // The macros' record, in the console table's columns.
    auto& vars = helix::ToolchangerVars::instance();
    if (auto st = vars.stats_for(name)) {
        const int rate = st->success_rate_pct();
        const std::string line = fmt::format(
            "PU {}  PD {}  FPU {}  FPD {}  REC {}  RATE {}", st->ups, st->downs, st->ups_failed,
            st->downs_failed, st->jiggled, rate < 0 ? std::string("--") : fmt::format("{}%", rate));
        lv_subject_copy_string(&stats_, line.c_str());
    } else {
        lv_subject_copy_string(&stats_, lv_tr("No changes recorded yet"));
    }

    // The slot's filament, and whether the macros think it is in the tool.
    std::string material = "--";
    if (auto* backend = AmsState::instance().get_backend()) {
        const SlotInfo info = backend->get_slot_info(tool_);
        if (info.slot_index >= 0 && !info.material.empty()) {
            material = info.material;
        }
    }
    lv_subject_copy_string(&material_, material.c_str());
    const int loaded = vars.loaded_for(name);
    lv_subject_copy_string(&loaded_, loaded == 1   ? lv_tr("loaded")
                                     : loaded == 0 ? lv_tr("empty")
                                                   : "");
}

void ToolActionsOverlay::set_busy(bool busy) {
    busy_ = busy;
    lv_subject_set_int(&busy_subject_, busy ? 1 : 0);
}

void ToolActionsOverlay::set_status(const std::string& text) {
    lv_subject_copy_string(&status_, text.c_str());
}

// ============================================================================
// ACTIONS
// ============================================================================

void ToolActionsOverlay::send(const std::string& gcode, const char* what) {
    auto* api = get_moonraker_api();
    if (!api) {
        set_status(lv_tr("Not connected"));
        return;
    }
    if (busy_) {
        return;
    }
    set_busy(true);
    set_status(fmt::format("{}: {}", what, gcode));
    spdlog::info("[{}] {} -> {}", get_name(), what, gcode);
    // The printer owes us the answer whether or not the overlay is still up,
    // so the completion rides object_lifetime_, not the screen's lifetime_.
    api->execute_gcode(
        gcode,
        object_lifetime_.bg_cb("ToolActions::ok",
                               [this, what]() {
                                   set_busy(false);
                                   set_status(fmt::format("{} {}", what, lv_tr("done")));
                                   refresh();
                               }),
        object_lifetime_.bg_cb("ToolActions::err",
                               [this, what](const MoonrakerError& err) {
                                   set_busy(false);
                                   set_status(fmt::format("{}: {}", what, err.user_message()));
                               }),
        IMoonrakerAPI::AMS_OPERATION_TIMEOUT_MS);
}

void ToolActionsOverlay::mount_or_dock() {
    auto& tools = helix::ToolState::instance();
    const auto& list = tools.tools();
    const bool mounted = tool_ >= 0 && tool_ < static_cast<int>(list.size()) &&
                         list[static_cast<size_t>(tool_)].active;
    if (mounted) {
        send(tool_macros::kDock, lv_tr("Dock"));
        return;
    }
    auto* api = get_moonraker_api();
    if (!api || busy_) {
        return;
    }
    set_busy(true);
    set_status(fmt::format("{} {}", lv_tr("Picking up"), tool_label(tool_)));
    tools.request_tool_change(
        tool_, api,
        object_lifetime_.bg_cb("ToolActions::mount_ok",
                               [this]() {
                                   set_busy(false);
                                   set_status(lv_tr("Tool change done"));
                                   refresh();
                               }),
        object_lifetime_.bg_cb("ToolActions::mount_err", [this](const std::string& err) {
            set_busy(false);
            set_status(err);
        }));
}

void ToolActionsOverlay::load() {
    send(fmt::format("{} TOOL={}", tool_macros::kLoad, tool_), lv_tr("Load"));
}

void ToolActionsOverlay::unload() {
    send(fmt::format("{} TOOL={}", tool_macros::kUnload, tool_), lv_tr("Unload"));
}

void ToolActionsOverlay::extrude() {
    send(fmt::format("{} TOOL={} LENGTH={}", tool_macros::kExtrude, tool_,
                     tool_macros::kExtrudeLengthMm),
         lv_tr("Extrude"));
}

void ToolActionsOverlay::retract() {
    send(fmt::format("{} TOOL={} LENGTH={}", tool_macros::kRetract, tool_,
                     tool_macros::kExtrudeLengthMm),
         lv_tr("Retract"));
}

void ToolActionsOverlay::change_filament() {
    auto* backend = AmsState::instance().get_backend();
    if (!backend || !parent_screen_) {
        set_status(lv_tr("No filament system to edit"));
        return;
    }
    const SlotInfo initial = backend->get_slot_info(tool_);
    get_ams_edit_overlay().show_for_slot(
        parent_screen_, tool_, initial, get_moonraker_api(),
        [](const AmsEditOverlay::EditResult& r) {
            if (!r.saved || r.slot_index < 0) {
                return;
            }
            auto* b = AmsState::instance().get_backend();
            if (!b) {
                return;
            }
            const SlotInfo original = b->get_slot_info(r.slot_index);
            const AmsError err =
                AmsState::instance().commit_slot_edit(r.slot_index, original, r.slot_info);
            if (!err.success()) {
                notify_ams_error(err);
            }
        });
}

// ============================================================================
// XML TRAMPOLINES
// ============================================================================

void ToolActionsOverlay::on_mount_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] mount");
    get_global_tool_actions_overlay().mount_or_dock();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolActionsOverlay::on_load_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] load");
    get_global_tool_actions_overlay().load();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolActionsOverlay::on_unload_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] unload");
    get_global_tool_actions_overlay().unload();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolActionsOverlay::on_extrude_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] extrude");
    get_global_tool_actions_overlay().extrude();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolActionsOverlay::on_retract_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] retract");
    get_global_tool_actions_overlay().retract();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolActionsOverlay::on_change_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] change");
    get_global_tool_actions_overlay().change_filament();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui

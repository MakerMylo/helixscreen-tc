// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_overlay_tool_actions.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"

#include "ams_state.h"
#include "app_globals.h"
#include "display_numbering.h"
#include "filament_database.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
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
    color_picker_.reset();
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
    UI_MANAGED_SUBJECT_STRING(rate_, rate_buf_, "--", "tool_act_rate", subjects_);
    UI_MANAGED_SUBJECT_INT(rate_state_, 0, "tool_act_rate_state", subjects_);
    UI_MANAGED_SUBJECT_STRING(mount_label_, mount_buf_, "", "tool_act_mount_label", subjects_);
    UI_MANAGED_SUBJECT_STRING(color_name_, color_name_buf_, "", "tool_act_color_name", subjects_);
    UI_MANAGED_SUBJECT_STRING(loaded_, loaded_buf_, "", "tool_act_loaded", subjects_);
    UI_MANAGED_SUBJECT_INT(dirty_, 0, "tool_act_dirty", subjects_);
    UI_MANAGED_SUBJECT_INT(busy_subject_, 0, "tool_act_busy", subjects_);
    UI_MANAGED_SUBJECT_STRING(status_, status_buf_, "", "tool_act_status", subjects_);

    static const std::pair<const char*, lv_event_cb_t> callbacks[] = {
        {"on_tool_act_mount", on_mount_clicked},
        {"on_tool_act_load", on_load_clicked},
        {"on_tool_act_unload", on_unload_clicked},
        {"on_tool_act_extrude", on_extrude_clicked},
        {"on_tool_act_retract", on_retract_clicked},
        {"on_tool_act_material_changed", on_material_changed},
        {"on_tool_act_pick_color", on_pick_color_clicked},
        {"on_tool_act_save", on_save_clicked},
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
    populate_material_dropdown();
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
    // A new tool starts a fresh form; edits to the previous one are dropped.
    refresh_form_from_slot();
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

    // The macros' record: clean pickups and drop-offs out of those attempted,
    // and the two together as one rate. Above kToolRateGoodPct it reads as
    // good; below, the tool wants a look.
    auto& vars = helix::ToolchangerVars::instance();
    if (auto st = vars.stats_for(name)) {
        const int rate = st->success_rate_pct();
        lv_subject_copy_string(&stats_,
                               fmt::format("{} {}/{} · {} {}/{}", lv_tr("Pick up"),
                                           st->ups - st->ups_failed, st->ups, lv_tr("Drop off"),
                                           st->downs - st->downs_failed, st->downs)
                                   .c_str());
        if (rate < 0) {
            lv_subject_copy_string(&rate_, "--");
            lv_subject_set_int(&rate_state_, 0);
        } else {
            lv_subject_copy_string(&rate_, fmt::format("{}%", rate).c_str());
            lv_subject_set_int(&rate_state_, rate > kToolRateGoodPct ? 1 : 2);
        }
    } else {
        lv_subject_copy_string(&stats_, lv_tr("No changes recorded yet"));
        lv_subject_copy_string(&rate_, "--");
        lv_subject_set_int(&rate_state_, 0);
    }

    // Whether the macros think filament is in the tool.
    const int loaded = vars.loaded_for(name);
    lv_subject_copy_string(&loaded_, loaded == 1   ? lv_tr("loaded")
                                     : loaded == 0 ? lv_tr("empty")
                                                   : "");

    // The slot may have changed under the form (another screen, Spoolman).
    // A form the user has not touched follows it; one with edits keeps them.
    if (auto* backend = AmsState::instance().get_backend()) {
        const SlotInfo now = backend->get_slot_info(tool_);
        const bool untouched = lv_subject_get_int(&dirty_) == 0;
        if (untouched &&
            (now.material != slot_original_.material || now.color_rgb != slot_original_.color_rgb ||
             now.slot_index != slot_original_.slot_index)) {
            refresh_form_from_slot();
        }
    }
}

// ============================================================================
// FILAMENT FORM
// ============================================================================

void ToolActionsOverlay::populate_material_dropdown() {
    if (!overlay_root_) {
        return;
    }
    lv_obj_t* dd = lv_obj_find_by_name(overlay_root_, "tool_act_material_dd");
    if (!dd) {
        return;
    }
    material_options_.clear();
    std::string options;
    for (const char* n : filament::get_all_material_names()) {
        if (!options.empty()) {
            options += '\n';
        }
        options += n;
        material_options_.emplace_back(n);
    }
    lv_dropdown_set_options(dd, options.c_str());
}

void ToolActionsOverlay::refresh_form_from_slot() {
    slot_original_ = SlotInfo{};
    if (auto* backend = AmsState::instance().get_backend()) {
        slot_original_ = backend->get_slot_info(tool_);
    }
    form_material_ = slot_original_.material;
    form_has_color_ =
        slot_original_.slot_index >= 0 && slot_original_.color_rgb != AMS_DEFAULT_SLOT_COLOR;
    form_color_ = slot_original_.color_rgb;
    form_color_name_ = slot_original_.color_name;
    if (form_has_color_ && form_color_name_.empty()) {
        form_color_name_ = get_color_name_from_hex(form_color_);
    }

    if (overlay_root_) {
        if (lv_obj_t* dd = lv_obj_find_by_name(overlay_root_, "tool_act_material_dd")) {
            if (material_options_.empty()) {
                populate_material_dropdown();
            }
            // The slot's material if the list has it, else the first entry
            // (and the form counts as changed to it).
            uint32_t sel = 0;
            bool found = false;
            for (size_t i = 0; i < material_options_.size(); ++i) {
                if (material_options_[i] == form_material_) {
                    sel = static_cast<uint32_t>(i);
                    found = true;
                    break;
                }
            }
            if (!found && !material_options_.empty() && form_material_.empty()) {
                form_material_ = material_options_[0];
            } else if (!found && !material_options_.empty()) {
                // Unknown material: add it so the dropdown can show it.
                lv_dropdown_add_option(dd, form_material_.c_str(), LV_DROPDOWN_POS_LAST);
                material_options_.push_back(form_material_);
                sel = static_cast<uint32_t>(material_options_.size() - 1);
            }
            lv_dropdown_set_selected(dd, sel);
        }
    }
    paint_color_block();
    update_dirty();
}

void ToolActionsOverlay::paint_color_block() {
    lv_subject_copy_string(&color_name_,
                           form_has_color_ ? form_color_name_.c_str() : lv_tr("Pick a colour"));
    if (!overlay_root_) {
        return;
    }
    if (lv_obj_t* block = lv_obj_find_by_name(overlay_root_, "tool_act_color_block")) {
        const lv_color_t c =
            form_has_color_ ? lv_color_hex(form_color_) : theme_manager_get_color("text_muted");
        // DECLARATIVE_OK: the form's pending colour is transient state with no
        // subject of its own; a colour cannot bind through XML.
        lv_obj_set_style_bg_color(block, c, LV_PART_MAIN);
    }
}

void ToolActionsOverlay::update_dirty() {
    const bool material_changed = form_material_ != slot_original_.material;
    const bool color_changed = form_has_color_ && form_color_ != slot_original_.color_rgb;
    lv_subject_set_int(&dirty_, (material_changed || color_changed) ? 1 : 0);
}

void ToolActionsOverlay::material_changed(int index) {
    if (index < 0 || static_cast<size_t>(index) >= material_options_.size()) {
        return;
    }
    form_material_ = material_options_[static_cast<size_t>(index)];
    update_dirty();
}

void ToolActionsOverlay::pick_color() {
    if (!parent_screen_) {
        return;
    }
    if (!color_picker_) {
        color_picker_ = std::make_unique<ColorPicker>();
    }
    color_picker_->set_color_callback([](uint32_t rgb, const std::string& name) {
        auto& self = get_global_tool_actions_overlay();
        self.form_color_ = rgb;
        self.form_color_name_ = name;
        self.form_has_color_ = true;
        self.paint_color_block();
        self.update_dirty();
    });
    color_picker_->show_with_color(parent_screen_, form_has_color_ ? form_color_ : 0x808080);
}

void ToolActionsOverlay::save_filament() {
    auto* backend = AmsState::instance().get_backend();
    if (!backend) {
        set_status(lv_tr("No filament system to edit"));
        return;
    }
    if (lv_subject_get_int(&dirty_) == 0) {
        return;
    }
    const SlotInfo original = backend->get_slot_info(tool_);
    SlotInfo edited = original;
    edited.material = form_material_;
    if (form_has_color_) {
        edited.color_rgb = form_color_;
        edited.color_name = form_color_name_;
        edited.multi_color_hexes.clear();
    }
    const AmsError err = AmsState::instance().commit_slot_edit(tool_, original, edited);
    if (!err.success()) {
        notify_ams_error(err);
        set_status(err.user_msg);
        return;
    }
    set_status(
        fmt::format("{}: {} {}", tool_label(tool_), lv_tr("filament saved"), form_material_));
    refresh_form_from_slot();
    refresh();
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

void ToolActionsOverlay::on_material_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] material");
    auto* dd = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (dd) {
        get_global_tool_actions_overlay().material_changed(
            static_cast<int>(lv_dropdown_get_selected(dd)));
    }
    LVGL_SAFE_EVENT_CB_END();
}

void ToolActionsOverlay::on_pick_color_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] pick colour");
    get_global_tool_actions_overlay().pick_color();
    LVGL_SAFE_EVENT_CB_END();
}

void ToolActionsOverlay::on_save_clicked(lv_event_t* e) {
    (void)e;
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolActions] save");
    get_global_tool_actions_overlay().save_filament();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::ui

// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include "async_lifetime_guard.h"
#include "moonraker_error.h"
#include "overlay_base.h"
#include "subject_managed_panel.h"

#include <lvgl.h>
#include <string>

/**
 * @file ui_overlay_tool_actions.h
 * @brief Per-tool actions for a tool changer, opened from the Tools panel.
 *
 * One overlay serves every tool: show_for_tool(n) points the tool_act_index
 * subject at the tool and everything on the page (the sideways path, the
 * labels) follows it.
 *
 * The buttons send the printer's own macros rather than G-code the screen
 * invents, because loading and extruding on a tool that is docked needs the
 * extruder activated and the temperature handled, which is the macro's job:
 *
 *   Pick up / Dock   klipper-toolchanger's tool change (ToolState) / UNSELECT_TOOL
 *   Load / Unload    LOAD_TOOL TOOL=n / UNLOAD_TOOL TOOL=n
 *   Extrude/Retract  TOOL_EXTRUDE TOOL=n LENGTH=l / TOOL_RETRACT TOOL=n LENGTH=l
 *   Change           the shared AMS slot editor for slot n
 *
 * The macro names live in helix::tool_macros so a printer with different
 * names changes one table.
 *
 * Subjects:
 * - tool_act_index        (int)    the tool shown; <tool_path> follows it
 * - tool_act_title        (string) "T3"
 * - tool_act_stats        (string) pickup/drop-off record, one line
 * - tool_act_mount_label  (string) "Pick up" or "Dock"
 * - tool_act_material     (string) the slot's material
 * - tool_act_loaded       (string) "loaded" / "empty" / "unknown" by memory
 * - tool_act_busy         (int)    an action is in flight
 * - tool_act_status       (string) last result line
 */
namespace helix::ui {

namespace tool_macros {
inline constexpr const char* kLoad = "LOAD_TOOL";
inline constexpr const char* kUnload = "UNLOAD_TOOL";
inline constexpr const char* kExtrude = "TOOL_EXTRUDE";
inline constexpr const char* kRetract = "TOOL_RETRACT";
inline constexpr const char* kDock = "UNSELECT_TOOL";
inline constexpr int kExtrudeLengthMm = 10;
} // namespace tool_macros

class ToolActionsOverlay : public OverlayBase {
  public:
    ToolActionsOverlay();
    ~ToolActionsOverlay() override;

    ToolActionsOverlay(const ToolActionsOverlay&) = delete;
    ToolActionsOverlay& operator=(const ToolActionsOverlay&) = delete;

    // === OverlayBase ===
    void init_subjects() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Tool Actions";
    }
    void on_activate() override;
    void on_deactivating(DeactivateReason reason) override;
    void cleanup() override;
    void on_ui_destroyed() override;

    /// Point the overlay at a tool and push it. Creates on first use.
    void show_for_tool(lv_obj_t* parent, int tool);

    [[nodiscard]] int tool() const {
        return tool_;
    }
    [[nodiscard]] bool is_busy() const {
        return busy_;
    }

    // Actions (public so a test can drive them)
    void mount_or_dock();
    void load();
    void unload();
    void extrude();
    void retract();
    void change_filament();

  private:
    void refresh();
    void send(const std::string& gcode, const char* what);
    void set_busy(bool busy);
    void set_status(const std::string& text);

    static void on_mount_clicked(lv_event_t* e);
    static void on_load_clicked(lv_event_t* e);
    static void on_unload_clicked(lv_event_t* e);
    static void on_extrude_clicked(lv_event_t* e);
    static void on_retract_clicked(lv_event_t* e);
    static void on_change_clicked(lv_event_t* e);

    int tool_ = 0;
    bool busy_ = false;

    char title_buf_[32] = "";
    char stats_buf_[128] = "";
    char mount_buf_[32] = "";
    char material_buf_[48] = "";
    char loaded_buf_[32] = "";
    char status_buf_[160] = "";
    lv_subject_t index_;
    lv_subject_t title_;
    lv_subject_t stats_;
    lv_subject_t mount_label_;
    lv_subject_t material_;
    lv_subject_t loaded_;
    lv_subject_t busy_subject_;
    lv_subject_t status_;
    SubjectManager subjects_;

    ObserverGuard tools_observer_;
    ObserverGuard vars_observer_;
    ObserverGuard slots_observer_;
};

ToolActionsOverlay& get_global_tool_actions_overlay();

} // namespace helix::ui

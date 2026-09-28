// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"
#include "ui_panel_base.h"

#include "helix/xml/indexed_subject_pool.h"
#include "subject_managed_panel.h"

#include <lvgl.h>
#include <string>
#include <vector>

/**
 * @file ui_panel_tools.h
 * @brief Tools panel: one column per tool changer tool.
 *
 * The tool changer's own view of its tools, in the shape of a multi-material
 * printer's filament display: a column per tool with the tool's name (tap it
 * to pick the tool up), its filament path drawn live by <tool_path>, the
 * slot's material and the extruder's temperature, and an actions button that
 * opens ToolActionsOverlay for that tool.
 *
 * What the columns bind to (all pools, sized before tools_count is published):
 * - tools_count          (int)    number of columns (ToolConfig's override,
 *                                 else the tools Klipper reported)
 * - tools_name_N         (string) the tool's G-code name ("T0")
 * - tools_material_N     (string) the slot's material, or "--"
 * - tools_temp_N         (string) "182°" / "182 / 250°" while heating
 *
 * The rest of the column (which tool is on the carriage, the path colours,
 * the sensor pips) reads ToolState, AmsState, ToolchangerVars and the sensor
 * manager directly, so this panel only publishes what has no subject of its
 * own already.
 */
namespace helix::ui {

class ToolsPanel : public PanelBase {
  public:
    ToolsPanel(helix::PrinterState& printer_state, IMoonrakerAPI* api);
    ~ToolsPanel() override;

    ToolsPanel(const ToolsPanel&) = delete;
    ToolsPanel& operator=(const ToolsPanel&) = delete;

    // === PanelBase ===
    void init_subjects() override;
    void setup(lv_obj_t* panel, lv_obj_t* parent_screen) override;
    const char* get_name() const override {
        return "Tools Panel";
    }
    const char* get_xml_component_name() const override {
        return "tools_panel";
    }
    void on_activate() override;

    /// Open the actions overlay for a tool (also the Options button's path).
    void open_actions(int tool);
    /// Pick a tool up (the tool-name button's path).
    void select_tool(int tool);

    lv_subject_t* get_count_subject() {
        return &count_;
    }

  protected:
    void on_deactivating(DeactivateReason reason) override;

  private:
    void refresh_columns();
    void refresh_column(int tool);
    void refresh_temps();
    void bind_temperatures();
    void on_extruder_temp(const std::string& extruder, int decideg, bool is_target);

    static void on_select_clicked(lv_event_t* e);
    static void on_opts_clicked(lv_event_t* e);
    /// The tool index encoded in a repeated widget's name ("tools_opts_3").
    static int tool_from_event(lv_event_t* e);

    bool ui_alive_ = false;
    lv_subject_t count_;
    helix::xml::IndexedSubjectPool names_{"tools_name",
                                          helix::xml::IndexedSubjectPool::Type::String, 32};
    helix::xml::IndexedSubjectPool materials_{"tools_material",
                                              helix::xml::IndexedSubjectPool::Type::String, 32};
    helix::xml::IndexedSubjectPool temps_{"tools_temp",
                                          helix::xml::IndexedSubjectPool::Type::String, 32};
    SubjectManager subjects_;

    ObserverGuard tools_observer_;
    ObserverGuard slots_observer_;
    ObserverGuard extruders_observer_;
    ObserverGuard config_observer_;
    /// One temp + one target observer per tool's extruder, rebound when the
    /// extruder set or the tool set changes.
    struct ExtruderWatch {
        std::string extruder;
        int temp_decideg = 0;
        int target_decideg = 0;
        ObserverGuard temp;
        ObserverGuard target;
    };
    std::vector<ExtruderWatch> watches_;
};

ToolsPanel& get_global_tools_panel();
void init_global_tools_panel(helix::PrinterState& printer_state, IMoonrakerAPI* api);

} // namespace helix::ui

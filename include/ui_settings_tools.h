// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_color_picker.h"

#include "overlay_base.h"
#include "subject_managed_panel.h"

#include <lvgl.h>
#include <memory>

/**
 * @file ui_settings_tools.h
 * @brief Tool Changer settings: tool count override and toolhead colours.
 *
 * Reached from Settings > Devices > Tool Changer (shown on a tool changer).
 * The page edits ToolConfig, which saves to the printer's section of
 * helixconfig.json and bumps tool_config_version so the Tools panel and its
 * path widgets repaint at once.
 *
 * Subjects:
 * - tools_cfg_detected  (string) "Klipper reports 6 tools" under the dropdown
 */
namespace helix::settings {

class ToolsSettingsOverlay : public OverlayBase {
  public:
    ToolsSettingsOverlay();
    ~ToolsSettingsOverlay() override;

    ToolsSettingsOverlay(const ToolsSettingsOverlay&) = delete;
    ToolsSettingsOverlay& operator=(const ToolsSettingsOverlay&) = delete;

    void init_subjects() override;
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    void show(lv_obj_t* parent_screen);

    const char* get_name() const override {
        return "Tool Changer Settings";
    }

    void on_activate() override;
    void on_deactivating(DeactivateReason reason) override;

    /// The count dropdown moved: index 0 is Auto, n is n tools.
    void handle_count_changed(int index);
    /// A colour row was tapped: open the picker for that tool.
    void handle_color_clicked(int tool);
    /// A row's Reset was tapped: back to the built-in colour.
    void handle_color_reset(int tool);

  private:
    void populate();
    void populate_count_dropdown();
    void populate_colors();
    int shown_tool_count() const;

    static void on_count_changed(lv_event_t* e);

    lv_obj_t* colors_list_ = nullptr;
    std::unique_ptr<helix::ui::ColorPicker> color_picker_;
    int picking_tool_ = -1;

    SubjectManager subjects_;
    char detected_buf_[64] = {};
    lv_subject_t detected_{};
};

ToolsSettingsOverlay& get_tools_settings_overlay();

} // namespace helix::settings

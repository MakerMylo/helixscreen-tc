// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// User settings for a tool changer's tools: how many columns the Tools panel
// shows and what colour each toolhead is drawn in.
//
// Klipper reports the tools it has, so the count is normally "auto"; the
// override exists for a printer that is not connected yet, or that reports
// tools the user does not want on the screen. The colours are the physical
// toolheads' (a StealthChanger with rainbow tools, say): they colour the
// toolhead glyph and the column's header bar, never the filament, which keeps
// the slot's own colour.
//
// Stored in the printer's section of helixconfig.json:
//   toolchanger/tool_count   int, 0 = auto (detected)
//   toolchanger/tool_colors  ["#E53935", "", ...]  "" = default for that index
//
// Main thread only; the version subject bumps on every saved change so the
// panel, the path widget and the settings page repaint.

#include "ui_observer_guard.h" // SubjectLifetime

#include "subject_managed_panel.h"

#include <cstdint>
#include <lvgl.h>
#include <string>
#include <vector>

namespace helix {

class ToolConfig {
  public:
    /// Upper bound on the count override and on stored colours.
    static constexpr int kMaxTools = 16;

    static ToolConfig& instance();

    ToolConfig(const ToolConfig&) = delete;
    ToolConfig& operator=(const ToolConfig&) = delete;

    /// Register `tool_config_version` (int). Idempotent.
    void init_subjects(bool register_xml = true);
    void deinit_subjects();

    /// Read the stored values for the active printer. Called by the subject
    /// initialiser and again whenever the active printer changes.
    void load();

    /// Configured column count, 0 = auto.
    [[nodiscard]] int tool_count() const {
        return tool_count_;
    }
    /// The count the panel shows: the override when set, else what Klipper
    /// reported.
    [[nodiscard]] int effective_tool_count(int detected) const {
        return tool_count_ > 0 ? tool_count_ : detected;
    }

    /// Colour of a toolhead: the user's, else the default for that index.
    [[nodiscard]] uint32_t color(int tool) const;
    /// True when the user has set this tool's colour.
    [[nodiscard]] bool has_custom_color(int tool) const;
    /// Human name for color(tool) ("Red", or the algorithmic name).
    [[nodiscard]] std::string color_name(int tool) const;

    /// The built-in palette: red, orange, yellow, green, blue, purple, then
    /// further distinct hues, cycling past the end.
    static uint32_t default_color(int tool);
    static const char* default_color_name(int tool);

    // Edits. Each one writes Config and saves; the version subject bumps
    // only when something changed.
    void set_tool_count(int count);
    void set_color(int tool, uint32_t rgb);
    void reset_color(int tool);

    lv_subject_t* get_version_subject() {
        return &tool_config_version_;
    }
    [[nodiscard]] SubjectLifetime get_subjects_lifetime() const {
        return subjects_.get_subjects_lifetime();
    }

    /// Test hook: forget everything without touching Config.
    void reset();

  private:
    ToolConfig() = default;
    void persist();
    void bump();

    int tool_count_ = 0;
    std::vector<std::string> colors_; ///< "#RRGGBB" or "" per index

    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    lv_subject_t tool_config_version_;
};

} // namespace helix

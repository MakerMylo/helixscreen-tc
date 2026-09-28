// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-tool bookkeeping a tool changer keeps in Klipper's [save_variables].
//
// klipper-toolchanger itself records nothing across restarts. A printer that
// wants pickup/drop-off statistics or a memory of which tool has filament in
// it writes them with SAVE_VARIABLE from its own macros, and Moonraker
// publishes the whole `save_variables.variables` dict on every change. This
// module is the ONLY place that knows the variable names and their shape:
//
//   tc_stats:  {"T0": {"ups": n, "downs": n, "ups_failed": n,
//                      "downs_failed": n, "jiggled": n}, ...}
//   tc_loaded: {"T0": 0|1, ...}   1 = filament is in the tool (by memory)
//   tc_last_tool: n               the tool last seen on the carriage
//
// The Tools panel asks this module questions; it never names the variables.
// A key that is absent is "unknown", never zero: Moonraker republishes only
// the fields that changed, so a frame without tc_stats is no news.

#include "ui_observer_guard.h" // SubjectLifetime

#include "subject_managed_panel.h"

#include <lvgl.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix {

class PrinterDiscovery;

/// Pickup / drop-off counters for one tool, as the macros record them.
struct ToolChangeStats {
    int ups = 0;          ///< pickups attempted
    int downs = 0;        ///< drop-offs attempted
    int ups_failed = 0;   ///< pickups that needed a hand
    int downs_failed = 0; ///< drop-offs that did not release
    int jiggled = 0;      ///< pickups that seated only after a retry

    bool operator==(const ToolChangeStats& o) const {
        return ups == o.ups && downs == o.downs && ups_failed == o.ups_failed &&
               downs_failed == o.downs_failed && jiggled == o.jiggled;
    }
    bool operator!=(const ToolChangeStats& o) const {
        return !(*this == o);
    }

    /// Percentage of changes (ups + downs) that went through cleanly, or -1
    /// when nothing has been recorded.
    [[nodiscard]] int success_rate_pct() const {
        const int total = ups + downs;
        if (total <= 0) {
            return -1;
        }
        const int good = total - ups_failed - downs_failed;
        return good * 100 / total;
    }
};

namespace toolchanger_vars {

/// Status objects the subscription must carry for this module to work:
/// `save_variables`, on a tool changer that has the object at all. Empty on
/// every other printer.
std::vector<std::string> required_status_objects(const PrinterDiscovery& hw);

} // namespace toolchanger_vars

/// Live copy of the tool changer's saved variables, fed from the status stream.
///
/// Main thread only, like ToolState: update_from_status() is called from the
/// same drained notification queue, and the subject it bumps drives XML.
class ToolchangerVars {
  public:
    static ToolchangerVars& instance();

    ToolchangerVars(const ToolchangerVars&) = delete;
    ToolchangerVars& operator=(const ToolchangerVars&) = delete;

    /// Register `tc_vars_version` (int, bumps on every change). Idempotent.
    void init_subjects(bool register_xml = true);
    void deinit_subjects();

    /// Apply a status frame. A frame without `save_variables` is ignored; a
    /// `variables` dict without one of our keys leaves that key's data alone.
    void update_from_status(const nlohmann::json& status);

    /// Counters for a tool by its klipper-toolchanger name ("T0"), or by
    /// number. Empty when nothing has been recorded for that tool.
    [[nodiscard]] std::optional<ToolChangeStats> stats_for(const std::string& tool_name) const;
    [[nodiscard]] std::optional<ToolChangeStats> stats_for(int tool_number) const;

    /// Whether filament is in the tool by the macros' memory: 1 loaded,
    /// 0 empty, -1 never recorded.
    [[nodiscard]] int loaded_for(const std::string& tool_name) const;
    [[nodiscard]] int loaded_for(int tool_number) const;

    /// The tool last recorded on the carriage, -1 if unknown.
    [[nodiscard]] int last_tool() const {
        return last_tool_;
    }

    /// True once a frame has carried tc_stats or tc_loaded at all.
    [[nodiscard]] bool has_data() const {
        return has_data_;
    }

    lv_subject_t* get_version_subject() {
        return &tc_vars_version_;
    }
    [[nodiscard]] SubjectLifetime get_subjects_lifetime() const {
        return subjects_.get_subjects_lifetime();
    }

    /// Test hook: forget everything, as a printer switch does.
    void reset();

  private:
    ToolchangerVars() = default;

    std::map<std::string, ToolChangeStats> stats_;
    std::map<std::string, int> loaded_;
    int last_tool_ = -1;
    bool has_data_ = false;

    bool subjects_initialized_ = false;
    SubjectManager subjects_;
    lv_subject_t tc_vars_version_;
};

} // namespace helix

// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_modal.h"

#include "ams_types.h"

#include <cstdint>

namespace helix::ui {

/// Lets the user tell the filament system its true state (tool, slot, whether
/// filament is loaded) and sends it through AmsBackend::recover_with_state().
/// Opened only for a backend that answers supports_recover_with_state().
class AmsRecoverStateModal : public Modal {
  public:
    /// What the three dropdowns offer. Tool: Unknown, then each tool. Slot:
    /// Unknown, each slot, then Bypass when the system has one. Filament:
    /// detect, Loaded, Unloaded.
    struct Choices {
        int tool_count = 0;
        int slot_count = 0;
        bool has_bypass = false;
    };

    /// Dropdown positions, in the order the rows appear.
    struct Selection {
        uint32_t tool = 0;
        uint32_t slot = 0;
        uint32_t loaded = 0;
    };

    const char* get_name() const override {
        return "AMS Recover State";
    }
    const char* component_name() const override {
        return "ams_recover_state_modal";
    }

    /// One-shot owned show over the active screen. False when there is no
    /// backend or it cannot take an asserted state; the caller falls back.
    static bool show_owned();

    /// The request the backend's live state implies, used to pre-fill.
    static RecoverStateRequest prefill(const AmsSystemInfo& info, bool bypass_active);

    static Selection selection_for(const RecoverStateRequest& request, const Choices& choices);
    static RecoverStateRequest request_for(const Selection& selection, const Choices& choices);

  protected:
    void on_show() override;
    void on_ok() override;

  private:
    Choices choices_;
};

} // namespace helix::ui

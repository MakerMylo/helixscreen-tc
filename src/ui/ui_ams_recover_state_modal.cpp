// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_ams_recover_state_modal.h"

#include "ui_error_reporting.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "display_numbering.h"
#include "lvgl/src/others/translation/lv_translation.h"

#include <spdlog/spdlog.h>

#include <lvgl.h>
#include <memory>
#include <string>

namespace helix::ui {

namespace {

constexpr uint32_t kLoadedDetect = 0;
constexpr uint32_t kLoadedYes = 1;
constexpr uint32_t kLoadedNo = 2;

void set_dropdown(lv_obj_t* dropdown, const std::string& options, uint32_t selected) {
    if (!dropdown) {
        return;
    }
    lv_dropdown_set_options(dropdown, options.c_str());
    lv_dropdown_set_selected(dropdown, selected);
}

uint32_t dropdown_selected(lv_obj_t* dropdown) {
    return dropdown ? lv_dropdown_get_selected(dropdown) : 0;
}

} // namespace

bool AmsRecoverStateModal::show_owned() {
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend || !backend->supports_recover_with_state()) {
        return false;
    }
    const std::string slot_label = noun_text(backend->lane_noun());
    const char* attrs[] = {"slot_label", slot_label.c_str(), nullptr};

    auto modal = std::make_unique<AmsRecoverStateModal>();
    if (!modal->show(lv_screen_active(), attrs)) {
        return false; // the unique_ptr frees the never-shown instance
    }
    lv_obj_t* backdrop = modal->backdrop();
    ModalStack::instance().assume_ownership(backdrop, std::move(modal));
    return true;
}

RecoverStateRequest AmsRecoverStateModal::prefill(const AmsSystemInfo& info, bool bypass_active) {
    RecoverStateRequest request;
    request.bypass = bypass_active;
    request.tool = info.current_tool >= 0 ? info.current_tool : -1;
    request.slot = info.current_slot >= 0 ? info.current_slot : -1;
    request.loaded = info.filament_loaded;
    return request;
}

AmsRecoverStateModal::Selection
AmsRecoverStateModal::selection_for(const RecoverStateRequest& request, const Choices& choices) {
    Selection s;
    if (request.bypass && choices.has_bypass) {
        s.slot = static_cast<uint32_t>(choices.slot_count) + 1;
    } else {
        if (request.tool >= 0 && request.tool < choices.tool_count) {
            s.tool = static_cast<uint32_t>(request.tool) + 1;
        }
        if (request.slot >= 0 && request.slot < choices.slot_count) {
            s.slot = static_cast<uint32_t>(request.slot) + 1;
        }
    }
    if (request.loaded.has_value()) {
        s.loaded = *request.loaded ? kLoadedYes : kLoadedNo;
    }
    return s;
}

RecoverStateRequest AmsRecoverStateModal::request_for(const Selection& selection,
                                                      const Choices& choices) {
    RecoverStateRequest request;
    const auto slot_count = static_cast<uint32_t>(choices.slot_count);
    if (choices.has_bypass && selection.slot == slot_count + 1) {
        request.bypass = true;
    } else {
        request.tool = static_cast<int>(selection.tool) - 1;
        request.slot = selection.slot <= slot_count ? static_cast<int>(selection.slot) - 1 : -1;
    }
    if (selection.loaded == kLoadedYes) {
        request.loaded = true;
    } else if (selection.loaded == kLoadedNo) {
        request.loaded = false;
    }
    return request;
}

void AmsRecoverStateModal::on_show() {
    wire_ok_button("btn_primary");
    wire_cancel_button("btn_secondary");

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }
    const AmsSystemInfo info = backend->get_system_info();
    choices_.slot_count = info.total_slots;
    choices_.tool_count = info.tool_to_slot_map.empty()
                              ? info.total_slots
                              : static_cast<int>(info.tool_to_slot_map.size());
    choices_.has_bypass = info.supports_bypass;

    const LaneNoun noun = backend->lane_noun();
    const Selection selected = selection_for(prefill(info, backend->is_bypass_active()), choices_);

    std::string tools = lv_tr("Unknown");
    for (int i = 0; i < choices_.tool_count; ++i) {
        tools += "\n" + lane_label(LaneNoun::Tool, i);
    }
    set_dropdown(find_widget("tool_dropdown"), tools, selected.tool);

    std::string slots = lv_tr("Unknown");
    for (int i = 0; i < choices_.slot_count; ++i) {
        slots += "\n" + lane_label(noun, i);
    }
    if (choices_.has_bypass) {
        slots += std::string("\n") + lv_tr("Bypass");
    }
    set_dropdown(find_widget("slot_dropdown"), slots, selected.slot);

    const std::string loaded = std::string(lv_tr("Detect automatically")) + "\n" + lv_tr("Loaded") +
                               "\n" + lv_tr("Unloaded");
    set_dropdown(find_widget("loaded_dropdown"), loaded, selected.loaded);
}

void AmsRecoverStateModal::on_ok() {
    Selection selection;
    selection.tool = dropdown_selected(find_widget("tool_dropdown"));
    selection.slot = dropdown_selected(find_widget("slot_dropdown"));
    selection.loaded = dropdown_selected(find_widget("loaded_dropdown"));
    const RecoverStateRequest request = request_for(selection, choices_);
    hide();

    // Re-fetched: the backend may have changed while the dialog was open.
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }
    AmsError err = backend->recover_with_state(request);
    if (!err.success()) {
        notify_ams_error(err, lv_tr("Recovery failed"));
    }
}

} // namespace helix::ui

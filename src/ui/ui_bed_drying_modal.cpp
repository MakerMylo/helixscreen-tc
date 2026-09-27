// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_bed_drying_modal.h"

#include "ui_event_safety.h"
#include "ui_toast_manager.h"
#include "ui_update_queue.h"

#include "app_globals.h"
#include "bed_drying_controller.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "printer_state.h"
#include "standard_macros.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <lvgl.h>
#include <memory>

namespace helix::ui {

using namespace bed_drying;

std::string BedDryingModal::material_label(const Material& m, int bed_c) {
    return fmt::format("{}  {}°C  {}h", m.name, bed_c, m.hours);
}

bool BedDryingModal::show_owned() {
    auto* ctrl = get_bed_drying_controller();
    if (!ctrl) {
        spdlog::warn("[BedDryingModal] no controller");
        return false;
    }
    auto modal = std::make_unique<BedDryingModal>();
    if (!modal->show(lv_screen_active())) {
        return false;
    }
    lv_obj_t* backdrop = modal->backdrop();
    ModalStack::instance().assume_ownership(backdrop, std::move(modal));
    return true;
}

void BedDryingModal::on_show() {
    wire_ok_button("btn_primary");
    wire_cancel_button("btn_secondary");

    auto* ctrl = get_bed_drying_controller();
    if (ctrl) {
        lv_subject_set_int(ctrl->get_ack_subject(), 0);
    }
    lv_obj_t* dropdown = find_widget("material_dropdown");
    if (!ctrl || !dropdown) {
        return;
    }
    std::string options;
    for (const auto& m : kMaterials) {
        if (!options.empty()) {
            options += '\n';
        }
        options += material_label(m, ctrl->bed_temp_for(m));
    }
    lv_dropdown_set_options(dropdown, options.c_str());
}

void BedDryingModal::on_ok() {
    lv_obj_t* dropdown = find_widget("material_dropdown");
    const auto index = dropdown ? lv_dropdown_get_selected(dropdown) : 0;
    lv_obj_t* appliance = find_widget("appliance_switch");
    const bool with_appliance = appliance && lv_obj_has_state(appliance, LV_STATE_CHECKED);
    const Material material = kMaterials[index < kMaterials.size() ? index : 0];
    hide();
    start_bed_drying_flow(material, with_appliance);
}

namespace {

void show_place_prompt() {
    modal_confirm(lv_tr("Place the spools"),
                  lv_tr("Clear the area above and below the plate. Lay the spools on the "
                        "plate, cover them with a box (a printed lid or the filament's "
                        "packaging) and close the door."),
                  ModalSeverity::Warning, lv_tr("Start drying"), [] {
                      if (auto* ctrl = get_bed_drying_controller()) {
                          ctrl->confirm_placed();
                      }
                  });
}

void prepare_plate(const Material& material, bool with_appliance) {
    auto* ctrl = get_bed_drying_controller();
    if (!ctrl) {
        return;
    }
    ToastManager::instance().show(ToastSeverity::INFO, lv_tr("Moving the plate..."), 3000);
    ctrl->prepare(material, with_appliance, show_place_prompt, [](const std::string& msg) {
        ToastManager::instance().show(
            ToastSeverity::ERROR,
            fmt::format("{}: {}", lv_tr("Could not move the plate"), msg).c_str(), 6000);
    });
}

} // namespace

void start_bed_drying_flow(const Material& material, bool with_appliance) {
    auto* ctrl = get_bed_drying_controller();
    if (!ctrl) {
        return;
    }
    const UnloadOffer offer = unload_offer(ctrl->toolhead_loaded());
    if (offer == UnloadOffer::None) {
        prepare_plate(material, with_appliance);
        return;
    }
    auto unload_then_prepare = [material, with_appliance] {
        IMoonrakerAPI* api = get_moonraker_api();
        const bool sent = StandardMacros::instance().execute(
            StandardMacroSlot::UnloadFilament, api,
            [material, with_appliance] {
                // The macro's reply arrives on the WebSocket thread.
                helix::ui::queue_update("BedDrying::unloaded", [material, with_appliance] {
                    prepare_plate(material, with_appliance);
                });
            },
            [](const MoonrakerError& err) {
                helix::ui::queue_update("BedDrying::unload_failed", [msg = err.message] {
                    ToastManager::instance().show(
                        ToastSeverity::ERROR,
                        fmt::format("{}: {}", lv_tr("Unload failed"), msg).c_str(), 6000);
                });
            },
            600000);
        if (!sent) {
            ToastManager::instance().show(ToastSeverity::WARNING,
                                          lv_tr("No unload macro found: unload by hand, then "
                                                "start again"),
                                          6000);
        }
    };
    ConfirmOptions opts;
    opts.cancel_text = lv_tr("Skip");
    opts.on_cancel = [material, with_appliance] { prepare_plate(material, with_appliance); };
    const char* message =
        offer == UnloadOffer::Recommended
            ? lv_tr("Filament is loaded at the toolhead. Unload it first, so it does not "
                    "soften in the extruder while the bed heats.")
            : lv_tr("Make sure no filament is loaded at the toolhead: it can soften in the "
                    "extruder while the bed heats.");
    modal_confirm(lv_tr("Unload filament?"), message,
                  offer == UnloadOffer::Recommended ? ModalSeverity::Warning : ModalSeverity::Info,
                  lv_tr("Unload filament"), unload_then_prepare, opts);
}

void show_bed_drying_remove_prompt() {
    modal_confirm(lv_tr("Remove the spools"),
                  lv_tr("The bed has cooled. Take the spools off the plate, then confirm."),
                  ModalSeverity::Info, lv_tr("Spools removed"), [] {
                      if (auto* ctrl = get_bed_drying_controller()) {
                          ctrl->confirm_removed();
                      }
                  });
}

void on_bed_drying_banner_clicked() {
    auto* ctrl = get_bed_drying_controller();
    if (!ctrl) {
        return;
    }
    switch (ctrl->state()) {
    case BedDryingController::State::Running:
        modal_confirm(lv_tr("Stop drying?"),
                      lv_tr("The bed turns off. The spools stay latched on the bed until you "
                            "confirm they are off."),
                      ModalSeverity::Warning, lv_tr("Stop"), [] {
                          if (auto* c = get_bed_drying_controller()) {
                              c->stop();
                          }
                      });
        break;
    case BedDryingController::State::Cooling:
        modal_confirm(lv_tr("Remove the spools now?"),
                      lv_tr("The plate and the spools are still hot. Use gloves."),
                      ModalSeverity::Warning, lv_tr("Spools removed"), [] {
                          if (auto* c = get_bed_drying_controller()) {
                              c->confirm_removed();
                          }
                      });
        break;
    case BedDryingController::State::ReadyToRemove:
        show_bed_drying_remove_prompt();
        break;
    case BedDryingController::State::Idle:
        break;
    }
}

namespace {

void on_bed_drying_banner_clicked_cb(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BedDrying] banner clicked");
    on_bed_drying_banner_clicked();
    LVGL_SAFE_EVENT_CB_END();
}

void on_bed_drying_start_clicked_cb(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[BedDrying] start clicked");
    BedDryingModal::show_owned();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace

void register_bed_drying_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_bed_drying_banner_clicked",
                             on_bed_drying_banner_clicked_cb);
    lv_xml_register_event_cb(nullptr, "on_bed_drying_start_clicked",
                             on_bed_drying_start_clicked_cb);
}

} // namespace helix::ui

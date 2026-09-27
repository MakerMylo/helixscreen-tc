// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_drying_controller.h"

#include "ui_notification.h"
#include "ui_timer_guard.h"

#include "ams_state.h"
#include "app_globals.h"
#include "filament_sensor_manager.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "panel_widget_manager.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "spdlog/spdlog.h"
#include "temperature_controller.h"

#include <spdlog/fmt/fmt.h>

#include <chrono>
#include <cstring>

namespace helix {

using namespace bed_drying;

namespace {

long long wall_clock_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::optional<bool> sensor_answer(const FilamentSensorManager& fsm, FilamentSensorRole role) {
    if (!fsm.is_master_enabled() || !fsm.is_sensor_available(role)) {
        return std::nullopt;
    }
    return fsm.is_filament_detected(role);
}

} // namespace

BedDryingController::BedDryingController(PrinterState& state, IMoonrakerAPI* api,
                                         TemperatureController* tc, Clock clock)
    : state_(state), api_(api), tc_(tc), clock_(clock ? std::move(clock) : Clock(wall_clock_s)) {}

BedDryingController::~BedDryingController() {
    cancel_timer();
    lifetime_.invalidate();
    if (subjects_initialized_) {
        subjects_initialized_ = false;
        subjects_.deinit_all();
    }
}

void BedDryingController::init_subjects() {
    if (subjects_initialized_) {
        return;
    }
    UI_MANAGED_SUBJECT_INT(bed_drying_state_, 0, "bed_drying_state", subjects_);
    UI_MANAGED_SUBJECT_STRING(bed_drying_text_, text_buf_, "", "bed_drying_text", subjects_);
    UI_MANAGED_SUBJECT_INT(bed_drying_ack_, 0, "bed_drying_ack", subjects_);
    subjects_initialized_ = true;
}

long long BedDryingController::now() const {
    return clock_();
}

void BedDryingController::cancel_timer() {
    if (timer_ && lv_is_initialized()) {
        ui::lv_timer_cancel_safe(timer_);
    }
    timer_ = nullptr;
}

std::optional<bool> BedDryingController::toolhead_loaded() const {
    const auto& fsm = FilamentSensorManager::instance();
    lv_subject_t* ams_loaded = AmsState::instance().get_filament_loaded_subject();
    return toolhead_loaded_from(sensor_answer(fsm, FilamentSensorRole::TOOLHEAD),
                                sensor_answer(fsm, FilamentSensorRole::RUNOUT),
                                ams_loaded && lv_subject_get_int(ams_loaded) != 0);
}

int BedDryingController::bed_temp_for(const Material& material) const {
    const int bed_max = tc_ ? static_cast<int>(tc_->keypad_range(HeaterType::Bed).max) : 0;
    return bed_temp_c(material, bed_max);
}

BedDryingController::State BedDryingController::state() const {
    if (!record_.latched) {
        return State::Idle;
    }
    if (!record_.ended) {
        return State::Running;
    }
    return removal_prompted_ ? State::ReadyToRemove : State::Cooling;
}

void BedDryingController::set_latch(bool on) {
    state_.set_spool_latch(on,
                           on && tc_ ? tc_->chamber_dryer_tokens() : std::vector<std::string>{});
}

void BedDryingController::restore() {
    record_ = SettingsManager::instance().get_bed_drying_record();
    if (!record_.latched) {
        publish();
        return;
    }
    spdlog::info("[BedDrying] Restoring a run: {} (spools on the bed)",
                 record_.ended ? "ended" : "in progress");
    set_latch(true);
    bed_target_seen_ = false;
    removal_prompted_ = false;
    if (!timer_) {
        timer_ = lv_timer_create(
            [](lv_timer_t* t) {
                auto* self = static_cast<BedDryingController*>(lv_timer_get_user_data(t));
                self->tick(self->now());
            },
            1000, this); // TIMER_DTOR_OK: cancelled in ~BedDryingController
    }
    tick(now());
}

void BedDryingController::prepare(const Material& material, bool with_appliance,
                                  std::function<void()> on_ready,
                                  std::function<void(const std::string&)> on_error) {
    if (!api_ || record_.latched) {
        if (on_error) {
            on_error(lv_tr("Drying is already running"));
        }
        return;
    }
    pending_material_ = material;
    pending_appliance_ = with_appliance;

    const AxisBounds b = state_.get_axis_bounds();
    const double x = b.has_x ? (b.x_min + b.x_max) / 2.0 : 0.0;
    const double y = b.has_y ? b.y_max - kClearanceMarginMm : 0.0;
    std::string move = fmt::format("G90\nG1 Z{:.1f} F600", clearance_z(b.z_max));
    if (b.has_x && b.has_y) {
        move += fmt::format("\nG1 X{:.1f} Y{:.1f} F6000", x, y);
    }
    move += "\nM400";

    auto tok = lifetime_.token();
    auto fail = [tok, on_error](const MoonrakerError& err) {
        if (tok.expired()) {
            return;
        }
        tok.defer("BedDrying::prepare_failed", [on_error, msg = err.message]() {
            if (on_error) {
                on_error(msg);
            }
        });
    };
    auto do_move = [this, tok, move, on_ready, fail]() {
        api_->execute_gcode(
            move,
            [tok, on_ready]() {
                if (tok.expired()) {
                    return;
                }
                tok.defer("BedDrying::placed_ready", [on_ready]() {
                    if (on_ready) {
                        on_ready();
                    }
                });
            },
            fail, 180000);
    };

    const char* homed = lv_subject_get_string(state_.get_homed_axes_subject());
    const bool all_homed =
        homed && std::strchr(homed, 'x') && std::strchr(homed, 'y') && std::strchr(homed, 'z');
    spdlog::info("[BedDrying] Preparing: {}clearance move to Z {:.1f}", all_homed ? "" : "home, ",
                 clearance_z(b.z_max));
    if (all_homed) {
        do_move();
        return;
    }
    api_->motion().home_axes(
        "",
        [tok, do_move]() {
            if (tok.expired()) {
                return;
            }
            tok.defer("BedDrying::homed", [do_move]() { do_move(); });
        },
        fail);
}

void BedDryingController::confirm_placed() {
    if (record_.latched || !api_) {
        return;
    }
    const Material& m = pending_material_;
    const long long start = now();
    record_ = RunRecord{};
    record_.latched = true;
    record_.start_s = start;
    record_.end_s = start + static_cast<long long>(m.hours) * 3600;
    record_.bed_c = bed_temp_for(m);
    record_.appliance = pending_appliance_ && tc_ && tc_->chamber_dryer().supported;

    // The latch reaches disk before any heat is sent.
    SettingsManager::instance().set_bed_drying_record(record_);
    set_latch(true);
    bed_target_seen_ = false;
    removal_prompted_ = false;
    spdlog::info("[BedDrying] Spools on the bed: {} at {}C for {}h{}", m.name, record_.bed_c,
                 m.hours, record_.appliance ? ", chamber dryer alongside" : "");

    if (tc_) {
        tc_->set_target(HeaterType::Bed, record_.bed_c);
        if (record_.appliance) {
            tc_->start_chamber_drying(static_cast<float>(record_.bed_c), m.hours * 60, false,
                                      false);
        }
        const long long end = record_.end_s;
        auto tok = lifetime_.token();
        tc_->read_configured_idle_timeout([this, tok, end](int configured_s) {
            if (tok.expired() || !record_.latched || record_.ended || record_.end_s != end ||
                !api_) {
                return;
            }
            record_.idle_restore_s = configured_s;
            SettingsManager::instance().set_bed_drying_record(record_);
            const long long hold = (end - now()) + kDeadManMarginS;
            api_->execute_gcode(fmt::format("SET_IDLE_TIMEOUT TIMEOUT={}", hold), nullptr, nullptr);
        });
    }
    restore();
}

void BedDryingController::stop() {
    if (record_.latched && !record_.ended) {
        end_run("stopped");
    }
}

// The bed goes off only while it is still ours: a target someone set by hand
// since owns it now. A target reading 0 is ours too; an off sent to a cold bed
// costs nothing.
void BedDryingController::end_run(const char* why) {
    spdlog::info("[BedDrying] Run ended ({})", why);
    record_.ended = true;
    SettingsManager::instance().set_bed_drying_record(record_);
    if (tc_) {
        lv_subject_t* target = state_.get_bed_target_subject();
        const int target_deci = target ? lv_subject_get_int(target) : 0;
        if (target_deci == 0 || target_deci == record_.bed_c * 10) {
            tc_->set_target(HeaterType::Bed, 0);
        }
        if (record_.appliance) {
            tc_->stop_chamber_drying();
        }
    }
    if (record_.idle_restore_s > 0 && api_) {
        api_->execute_gcode(fmt::format("SET_IDLE_TIMEOUT TIMEOUT={}", record_.idle_restore_s),
                            nullptr, nullptr);
    }
    publish();
}

void BedDryingController::confirm_removed() {
    if (!record_.latched) {
        return;
    }
    if (!record_.ended) {
        end_run("spools removed");
    }
    spdlog::info("[BedDrying] Spools removed; latch cleared");
    record_ = RunRecord{};
    SettingsManager::instance().clear_bed_drying_record();
    set_latch(false);
    removal_prompted_ = false;
    cancel_timer();
    publish();
}

void BedDryingController::tick(long long now_s) {
    if (!record_.latched) {
        publish();
        return;
    }
    // A run restored before discovery registered no dryer tokens; picking them
    // up here lets the dryer's stop through once the appliance is known.
    set_latch(true);
    if (!record_.ended) {
        lv_subject_t* target = state_.get_bed_target_subject();
        const int target_deci = target ? lv_subject_get_int(target) : 0;
        if (target_deci == record_.bed_c * 10) {
            bed_target_seen_ = true;
        }
        if (phase_at(now_s, record_.end_s) == Phase::Ended) {
            end_run("cycle complete");
        } else if (bed_target_seen_ && target_deci == 0) {
            // Klipper dropped the bed target: a restart, an emergency stop, or
            // its idle timeout. The run is over either way.
            end_run("bed turned off");
        } else if (!record_.flip_notified && flip_due(now_s, record_.start_s, record_.end_s)) {
            record_.flip_notified = true;
            SettingsManager::instance().set_bed_drying_record(record_);
            ui_notification_info(lv_tr("Flip the spools"),
                                 lv_tr("Halfway through drying: flip the spools over. Use "
                                       "gloves, the plate is hot."));
        }
    }
    if (record_.ended && !removal_prompted_) {
        lv_subject_t* temp = state_.get_bed_temp_subject();
        const double bed_c = temp ? lv_subject_get_int(temp) / 10.0 : 0.0;
        if (may_prompt_removal(bed_c)) {
            removal_prompted_ = true;
            publish();
            if (on_ready_to_remove_) {
                on_ready_to_remove_();
            }
            return;
        }
    }
    publish();
}

void BedDryingController::publish() {
    if (!subjects_initialized_) {
        return;
    }
    const State s = state();
    std::string text;
    switch (s) {
    case State::Running: {
        const long long left = std::max(0LL, record_.end_s - now());
        text = fmt::format("{} {}°C  {}:{:02d} {}", lv_tr("Drying on the bed"), record_.bed_c,
                           left / 3600, (left % 3600) / 60, lv_tr("left"));
        break;
    }
    case State::Cooling: {
        lv_subject_t* temp = state_.get_bed_temp_subject();
        const int bed_c = temp ? lv_subject_get_int(temp) / 10 : 0;
        text = fmt::format("{} {}°C", lv_tr("Bed cooling, spools still on the bed:"), bed_c);
        break;
    }
    case State::ReadyToRemove:
        text = lv_tr("Remove the spools from the bed");
        break;
    case State::Idle:
        break;
    }
    if (lv_subject_get_int(&bed_drying_state_) != static_cast<int>(s)) {
        lv_subject_set_int(&bed_drying_state_, static_cast<int>(s));
    }
    if (text != lv_subject_get_string(&bed_drying_text_)) {
        lv_subject_copy_string(&bed_drying_text_, text.c_str());
    }
}

BedDryingController* get_bed_drying_controller() {
    return PanelWidgetManager::instance().shared_resource<BedDryingController>();
}

} // namespace helix

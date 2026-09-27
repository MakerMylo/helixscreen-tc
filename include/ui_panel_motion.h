// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_observer_guard.h"

#include "axis.h"
#include "hold_repeat_timer.h"
#include "jog_coalescer.h"
#include "overlay_base.h"
#include "subject_managed_panel.h"

#include <array>
#include <optional>

/**
 * @file ui_panel_motion.h
 * @brief Motion panel - XYZ movement and homing control
 *
 * Overlay panel for jogging the printer head in X/Y/Z directions and homing axes.
 * Uses OverlayBase pattern with lifecycle hooks.
 */

// Jog mode: determines inner/outer ring distances for XY pad and Z buttons
namespace helix {

struct AxisBounds;

/// Keypad parameters for tapping one axis readout in the motion header.
struct AxisKeypadParams {
    float min_value;     ///< Axis envelope minimum (mm)
    float max_value;     ///< Axis envelope maximum (mm)
    float seed;          ///< COMMANDED position (mm); always commanded, even in actual mode
    bool allow_negative; ///< True only when the axis minimum is below zero
};

/// Seed and bounds for one axis' coordinate keypad. Empty when the axis
/// envelope is unknown: an absolute move would have nothing to clamp against.
std::optional<AxisKeypadParams> keypad_params_for_axis(const AxisBounds& bounds, Axis axis,
                                                       double commanded_mm);

enum class JogMode { Fine = 0, Coarse = 1, Turbo = 2 };
constexpr int JOG_MODE_COUNT = 3;

// Inner/outer distance pair for each mode. Labels are owned buffers so the
// struct can travel by value out of the settings lookup.
struct JogModeDistances {
    float inner;
    float outer;
    char inner_label[16];
    char outer_label[16];
};

/// Inner and outer ring distances for a jog mode, in millimetres.
/// Values come from settings; the labels are formatted for display.
JogModeDistances get_jog_mode_distances(JogMode mode);

inline const char* jog_mode_name(JogMode mode) {
    static const char* names[] = {"Fine", "Coarse", "Turbo"};
    int idx = static_cast<int>(mode);
    if (idx < 0 || idx >= JOG_MODE_COUNT)
        return "Unknown";
    return names[idx];
}

// Jog direction
enum class JogDirection {
    N,  // +Y
    S,  // -Y
    E,  // +X
    W,  // -X
    NE, // +X+Y
    NW, // -X+Y
    SE, // +X-Y
    SW  // -X-Y
};
} // namespace helix

class MotionPanel : public OverlayBase {
  public:
    MotionPanel();
    ~MotionPanel() override;

    // === OverlayBase interface ===
    void init_subjects() override;
    void deinit_subjects();
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;
    const char* get_name() const override {
        return "Motion Panel";
    }

    // === Lifecycle hooks ===
    void on_activate() override;
    void on_deactivating(DeactivateReason reason) override;
    void on_ui_destroyed() override;

    // === Public API ===
    lv_obj_t* get_panel() const {
        return overlay_root_;
    }
    void set_position(float x, float y, float z);
    helix::JogMode get_jog_mode() const {
        return current_mode_;
    }

    /// Jog one zone. Returns false when nothing was dispatched (fully clamped
    /// at a limit, or the send was refused) - a hold-to-repeat reading false
    /// stops repeating.
    bool jog(helix::JogDirection direction, float distance_mm);
    void home(char axis);

    /// Jog one Z button. Same false-means-refused contract as jog().
    bool handle_z_button(const char* name);
    void set_jog_mode(helix::JogMode mode); // Switch between Fine/Coarse/Turbo jog mode

    /// The Z hold timer, for the XML pressed/released/press_lost callbacks.
    /// Also the test seam: poll() drives the repeat without a periodic timer,
    /// which the unit-test harness never runs.
    helix::HoldRepeatTimer& z_hold_timer() {
        return z_hold_timer_;
    }

    /// Arm the Z hold repeat for a named button press (the XML pressed event).
    void begin_z_hold(const char* button_name);

    /// Flip the persisted commanded/actual coordinate preference. The panel's
    /// observer on the settings subject re-renders the readouts.
    void toggle_coordinate_source();

    /// Open the coordinate keypad for one axis ('x'/'y'/'z'), seeded with the
    /// COMMANDED position and bounded by the axis envelope. Refuses (no-op)
    /// while jogging is gated off (not connected or klippy not ready).
    void open_axis_keypad(char axis);

    /// Keypad confirm: home the axis if needed, then dispatch the absolute
    /// target for that one axis.
    void request_axis_target(char axis, double mm);

    /// Clamp one axis against its bounds, raising at most one warning per
    /// approach. Returns the permitted delta, 0.0 when fully blocked.
    double clamp_axis_and_warn(helix::Axis axis, double current, double uncommitted, double delta,
                               float min, float max);

  private:
    // RAII subject manager - auto-deinits all registered subjects on destruction
    SubjectManager subjects_;

    lv_subject_t pos_x_subject_;
    lv_subject_t pos_y_subject_;
    lv_subject_t pos_z_subject_;          // Commanded Z position
    lv_subject_t z_axis_label_subject_;   // "Bed" or "Print Head"
    lv_subject_t z_up_icon_subject_;      // "arrow_expand_up" or "arrow_up"
    lv_subject_t z_down_icon_subject_;    // "arrow_expand_down" or "arrow_down"
    lv_subject_t z_large_label_subject_;  // "10mm" or "1mm" (large Z button label)
    lv_subject_t z_small_label_subject_;  // "1mm" or "0.1mm" (small Z button label)
    lv_subject_t jog_mode_fine_active_;   // 1 when Fine mode active
    lv_subject_t jog_mode_coarse_active_; // 1 when Coarse mode active
    lv_subject_t jog_mode_turbo_active_;  // 1 when Turbo mode active
    // Per-axis homing state (0=unhomed, 1=homed) for declarative bind_style:
    // the coordinate readouts mute an axis whose position is not trustworthy.
    lv_subject_t motion_x_homed_;
    lv_subject_t motion_y_homed_;
    lv_subject_t motion_z_homed_;
    char pos_x_buf_[32];
    char pos_y_buf_[32];
    char pos_z_buf_[32];
    char z_axis_label_buf_[16];
    char z_up_icon_buf_[24];
    char z_down_icon_buf_[24];
    char z_large_label_buf_[8];
    char z_small_label_buf_[8];
    bool bed_moves_ = false; // If true, invert Z direction (arrows match bed movement)

    helix::JogMode current_mode_ = helix::JogMode::Coarse;
    float current_x_ = 0.0f;
    float current_y_ = 0.0f;
    float current_z_ = 0.0f; // Gcode (commanded) Z position

    int gcode_z_centimm_ = 0;

    lv_obj_t* jog_pad_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    bool callbacks_registered_ = false;

    helix::JogCoalescer jog_coalescer_;
    /// Z the toolhead will sit at when the latest target's script starts,
    /// captured at enqueue time. Feeds move_to's travel-before-descend
    /// ordering; delta moves ignore it.
    double target_start_z_ = 0.0;
    /// Per-axis "blocked at limit" latch, indexed with helix::axis_index().
    /// Repeated attempts against a limit must not each raise a toast, and
    /// hold-to-repeat makes that a flood rather than a nuisance.
    std::array<bool, 3> edge_warned_{};

    /// Hold-to-repeat for the four Z buttons. The pad owns its own repeat
    /// (it is the only one that can see the press point move between zones);
    /// both use helix::HoldRepeatTimer so the timing logic is shared.
    helix::HoldRepeatTimer z_hold_timer_;
    char z_hold_button_[16] = {};

    /// stop_hold_repeat() sweeps both this timer and the pad's on every
    /// teardown path.
    void stop_hold_repeat();
    static bool z_hold_fire(void* user_data);

    // Route a tap/flush through the coalescer and send if idle. Returns
    // whether the move was dispatched or accepted as pending.
    bool dispatch_jog(const helix::AxisMove& delta);
    // Route an absolute target through the coalescer and send if idle.
    bool dispatch_target(const helix::AxisTarget& target);
    // Send one coalesced move (delta or target); ack/error callbacks re-enter
    // the coalescer. Returns false when there is no API to send through.
    bool send_jog_move(const helix::JogCoalescer::CoalescedMove& move);

    ObserverGuard position_x_observer_;
    ObserverGuard position_y_observer_;
    ObserverGuard gcode_z_observer_;
    ObserverGuard live_position_observer_x_;
    ObserverGuard live_position_observer_y_;
    ObserverGuard live_position_observer_z_;
    ObserverGuard coordinate_mode_observer_;
    ObserverGuard bed_moves_observer_;
    ObserverGuard homed_axes_observer_;
    ObserverGuard jog_ready_observer_;

    // Actual (live) positions in mm; the readouts show these when the
    // coordinate preference is "actual", the commanded current_x_/y_/z_
    // otherwise. Keypad seed and jog math always use the commanded values.
    float live_x_ = 0.0f;
    float live_y_ = 0.0f;
    float live_z_ = 0.0f;

    /// Axis whose keypad is open ('x'/'y'/'z'); the keypad callback is a bare
    /// function pointer, so the axis travels through the panel.
    char keypad_axis_ = 'x';

    /// Re-render all three readout subjects from the selected source.
    void refresh_position_display();

    /// ui_keypad_show() confirm callback.
    static void on_axis_keypad_value(float value, void* user_data);

    void setup_jog_pad();
    void register_position_observers();

    // Enable/dim the jog pad from the current nav_buttons_enabled state
    // (connected AND klippy ready). Jogging while not ready is refused at the
    // API layer; this makes the pad visibly unavailable to match.
    void update_jog_pad_enabled();

    static bool jog_pad_jog_cb(helix::JogDirection direction, float distance_mm, void* user_data);
    static void jog_pad_home_cb(void* user_data);
    // Position observers use lambda-based observer factory (no static callbacks needed)

    void update_z_axis_label(bool bed_moves);
    void update_z_button_labels(); // Update Z button text for current mode

    /// Which source the readouts render: 1 = actual (live) position,
    /// 0 = commanded. Mirrors the persisted preference subject.
    bool show_actual_ = false;
};

MotionPanel& get_global_motion_panel();

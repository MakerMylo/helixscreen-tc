// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_motion_panel_coordinates.cpp
 * @brief The coordinate readout renders commanded or actual (live) position
 *
 * Run with: ./build/bin/helix-tests "[motion][coords]"
 *
 * The header readouts (motion_pos_x/y/z subjects) show the commanded gcode
 * position by default and the live position while the persisted preference
 * says "actual". Flipping the SettingsManager subject re-renders through the
 * panel's coordinate-mode observer, and the swap glyph recolors with it.
 * keypad_params_for_axis() decides the keypad's seed and bounds from the axis
 * envelope: always the commanded seed, negative input only where the axis
 * minimum is below zero, nothing when the envelope is unknown.
 */

#include "ui_nav_manager.h"
#include "ui_panel_motion.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "app_globals.h"
#include "printer_motion_state.h"
#include "settings_manager.h"
#include "static_panel_registry.h"
#include "theme_manager.h"
#include "ui/ui_lazy_panel_helper.h"
#include "unit_conversions.h"

#include <array>
#include <cstring>
#include <lvgl.h>

#include "../catch_amalgamated.hpp"

namespace {

bool same_color(const lv_color_t& a, const lv_color_t& b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

void check_header_text(lv_obj_t* root, const char* name, const char* expected) {
    lv_obj_t* label = lv_obj_find_by_name(root, name);
    REQUIRE(label != nullptr);
    CHECK(std::strcmp(lv_label_get_text(label), expected) == 0);
}

} // namespace

TEST_CASE("keypad params seed the commanded value and bound by the envelope", "[motion][coords]") {
    helix::AxisBounds bounds;
    bounds.has_x = true;
    bounds.x_min = 0.0f;
    bounds.x_max = 350.0f;
    bounds.has_z = true;
    bounds.z_min = -5.0f;
    bounds.z_max = 200.0f;

    const auto x = helix::keypad_params_for_axis(bounds, helix::Axis::X, 123.4);
    REQUIRE(x.has_value());
    CHECK(x->seed == Catch::Approx(123.4f));
    CHECK(x->min_value == Catch::Approx(0.0f));
    CHECK(x->max_value == Catch::Approx(350.0f));
    CHECK_FALSE(x->allow_negative);

    // Z's minimum is below zero, so negative input is allowed there.
    const auto z = helix::keypad_params_for_axis(bounds, helix::Axis::Z, -1.25);
    REQUIRE(z.has_value());
    CHECK(z->seed == Catch::Approx(-1.25f));
    CHECK(z->min_value == Catch::Approx(-5.0f));
    CHECK(z->max_value == Catch::Approx(200.0f));
    CHECK(z->allow_negative);

    // Y has no known envelope: an absolute move would have nothing to clamp
    // against, so there is nothing to open a keypad with.
    CHECK_FALSE(helix::keypad_params_for_axis(bounds, helix::Axis::Y, 10.0).has_value());
}

TEST_CASE_METHOD(LVGLUITestFixture, "coordinate readouts follow the commanded/actual preference",
                 "[motion][coords][xml]") {
    std::array<lv_obj_t*, UI_PANEL_COUNT> panels{};
    for (auto& p : panels)
        p = lv_obj_create(lv_screen_active());
    NavigationManager::instance().set_panels(panels.data());

    auto& settings = helix::SettingsManager::instance();
    settings.init_subjects();
    settings.set_motion_show_actual_position(false);

    lv_obj_t* cached = nullptr;
    REQUIRE(helix::ui::lazy_create_and_push_overlay<MotionPanel>(
        get_global_motion_panel, cached, lv_screen_active(), "Motion", "test"));
    helix::ui::UpdateQueue::instance().drain();

    lv_obj_t* root = get_global_motion_panel().get_root();
    REQUIRE(root != nullptr);

    auto& ps = get_printer_state();
    using helix::units::to_centimm;
    lv_subject_set_int(ps.get_gcode_position_x_subject(), to_centimm(10.0));
    lv_subject_set_int(ps.get_gcode_position_y_subject(), to_centimm(20.0));
    lv_subject_set_int(ps.get_gcode_position_z_subject(), to_centimm(5.0));
    lv_subject_set_int(ps.get_live_position_x_subject(), to_centimm(11.5));
    lv_subject_set_int(ps.get_live_position_y_subject(), to_centimm(21.5));
    lv_subject_set_int(ps.get_live_position_z_subject(), to_centimm(5.25));
    helix::ui::UpdateQueue::instance().drain();

    // Commanded mode (the default): gcode position, not live.
    check_header_text(root, "header_pos_x", "10.00");
    check_header_text(root, "header_pos_y", "20.00");
    check_header_text(root, "header_pos_z", "5.00");

    // The swap glyph reads muted while the preference is "commanded".
    lv_obj_t* swap = lv_obj_find_by_name(root, "header_pos_swap");
    REQUIRE(swap != nullptr);
    lv_obj_t* glyph = lv_obj_get_child(swap, 0);
    REQUIRE(glyph != nullptr);
    CHECK(same_color(lv_obj_get_style_text_color(glyph, LV_PART_MAIN),
                     theme_manager_get_color("text_muted")));

    settings.set_motion_show_actual_position(true);
    helix::ui::UpdateQueue::instance().drain();

    // Actual mode: live position, re-rendered by the flip.
    check_header_text(root, "header_pos_x", "11.50");
    check_header_text(root, "header_pos_y", "21.50");
    check_header_text(root, "header_pos_z", "5.25");
    CHECK(same_color(lv_obj_get_style_text_color(glyph, LV_PART_MAIN),
                     theme_manager_get_color("primary")));

    // Live updates keep flowing while the preference is "actual"...
    lv_subject_set_int(ps.get_live_position_x_subject(), to_centimm(12.75));
    helix::ui::UpdateQueue::instance().drain();
    check_header_text(root, "header_pos_x", "12.75");

    // ...and commanded updates win again once it flips back.
    settings.set_motion_show_actual_position(false);
    lv_subject_set_int(ps.get_gcode_position_x_subject(), to_centimm(13.0));
    helix::ui::UpdateQueue::instance().drain();
    check_header_text(root, "header_pos_x", "13.00");

    StaticPanelRegistry::instance().destroy_all();
    helix::ui::UpdateQueue::instance().drain();
}

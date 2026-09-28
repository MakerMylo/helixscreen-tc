// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_panel_settings.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "settings_manager.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {
struct RootFixture : LVGLUITestFixture {
    lv_obj_t* root_ = nullptr;
    RootFixture() {
        // The test binary's stub app_globals_init_subjects() (tests/ui_test_utils.cpp)
        // never runs PrinterCapabilitiesState::init_subjects(), so printer_has_speaker
        // is otherwise absent and the Sound row's gate resolves to nothing.
        static lv_subject_t speaker_subject;
        if (!lv_xml_get_subject(nullptr, "printer_has_speaker")) {
            lv_subject_init_int(&speaker_subject, 1);
            lv_xml_register_subject(nullptr, "printer_has_speaker", &speaker_subject);
        }
        SettingsManager::instance().init_subjects();
        get_global_settings_panel().init_subjects();
        root_ = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "settings_panel", nullptr));
        REQUIRE(root_ != nullptr);
        process_lvgl(5);
    }
    ~RootFixture() override {
        if (root_ && lv_obj_is_valid(root_))
            lv_obj_delete(root_);
        helix::ui::UpdateQueue::instance().drain();
        get_global_settings_panel().deinit_subjects();
        helix::ui::UpdateQueue::instance().drain();
    }
    lv_obj_t* find(const char* n) const {
        return lv_obj_find_by_name(root_, n);
    }
    void set_int(const char* subject, int v) {
        lv_subject_t* s = lv_xml_get_subject(nullptr, subject);
        REQUIRE(s != nullptr);
        lv_subject_set_int(s, v);
        process_lvgl(5);
    }
};
} // namespace

TEST_CASE_METHOD(RootFixture, "settings root: three groups, twelve rows, in order",
                 "[settings][settings_root]") {
    const std::vector<std::pair<const char*, std::vector<const char*>>> groups = {
        {"group_screen", {"row_display", "row_appearance", "row_touch_input", "row_sound"}},
        {"group_printer", {"row_printing", "row_devices", "row_safety", "row_connection"}},
        {"group_helixscreen", {"row_language_time", "row_system", "row_updates", "row_help"}},
    };
    for (const auto& [group, rows] : groups) {
        lv_obj_t* g = find(group);
        REQUIRE(g != nullptr);
        int last_index = -1;
        for (const char* r : rows) {
            CAPTURE(group, r);
            lv_obj_t* row = lv_obj_find_by_name(g, r);
            REQUIRE(row != nullptr);
            int idx = lv_obj_get_index(row);
            CHECK(idx > last_index);
            last_index = idx;
        }
    }
    CHECK(find("row_display_sound") == nullptr);
    CHECK(find("row_hardware") == nullptr);
}

TEST_CASE_METHOD(RootFixture, "settings root: Sound hides without a speaker",
                 "[settings][settings_root]") {
    set_int("printer_has_speaker", 0);
    CHECK(lv_obj_has_flag(find("row_sound"), LV_OBJ_FLAG_HIDDEN));
    set_int("printer_has_speaker", 1);
    CHECK_FALSE(lv_obj_has_flag(find("row_sound"), LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(RootFixture, "settings root: Updates hides only when no update row would show",
                 "[settings][settings_root]") {
    set_int("show_update_settings", 0);
    set_int("updates_firmware_managed", 0);
    set_int("updates_unavailable", 0);
    CHECK(lv_obj_has_flag(find("row_updates"), LV_OBJ_FLAG_HIDDEN));
    set_int("updates_firmware_managed", 1);
    CHECK_FALSE(lv_obj_has_flag(find("row_updates"), LV_OBJ_FLAG_HIDDEN));
    set_int("updates_firmware_managed", 0);
    set_int("show_update_settings", 1);
    CHECK_FALSE(lv_obj_has_flag(find("row_updates"), LV_OBJ_FLAG_HIDDEN));
}

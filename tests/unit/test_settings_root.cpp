// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_panel_settings.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/scoped_runtime_config.h"
#include "app_globals.h"
#include "display_settings_manager.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "printer_state.h"
#include "settings_manager.h"
#include "system_settings_manager.h"

#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace {
struct RootFixture : LVGLUITestFixture {
    // Declared first so it is torn down LAST (reverse declaration order):
    // refresh_status_lines() reads get_wifi_manager() (a process-lifetime
    // singleton) and constructs a member EthernetManager, and without
    // test_mode both pick real platform backends — WifiBackend::create()'s
    // start_async() hands the singleton a worker thread with no join site
    // here (prestonbrown/helixscreen#1531). Forcing the mock backends keeps
    // every case in this file hermetic and gives EthernetBackendMock's
    // connected=true a deterministic "Ethernet" status to assert against.
    ScopedRuntimeConfig scoped_config_;
    lv_obj_t* root_ = nullptr;
    RootFixture() {
        get_runtime_config()->test_mode = true;
        get_runtime_config()->use_real_wifi = false;
        get_runtime_config()->use_real_ethernet = false;

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

TEST_CASE_METHOD(RootFixture, "settings root: Updates shows on updates_unavailable alone",
                 "[settings][settings_root]") {
    set_int("show_update_settings", 0);
    set_int("updates_firmware_managed", 0);
    set_int("updates_unavailable", 1);
    CHECK_FALSE(lv_obj_has_flag(find("row_updates"), LV_OBJ_FLAG_HIDDEN));
}

namespace {
std::string status_text(lv_obj_t* root, const char* row) {
    lv_obj_t* r = lv_obj_find_by_name(root, row);
    REQUIRE(r != nullptr);
    lv_obj_t* s = lv_obj_find_by_name(r, "status");
    REQUIRE(s != nullptr);
    return lv_label_get_text(s);
}
} // namespace

TEST_CASE_METHOD(RootFixture, "settings root: status lines follow values on return",
                 "[settings][settings_root]") {
    set_int("settings_sounds_enabled", 1);
    set_int("settings_volume", 40);
    get_global_settings_panel().refresh_status_lines();
    process_lvgl(5);
    CHECK(status_text(root_, "row_sound") == "Volume 40%");

    set_int("settings_volume", 0);
    CHECK(status_text(root_, "row_sound") == "Volume 40%"); // no observer: stale until return
    get_global_settings_panel().on_activate();
    process_lvgl(5);
    CHECK(status_text(root_, "row_sound") == "Muted");
}

TEST_CASE_METHOD(RootFixture, "settings root: rows without state show no status",
                 "[settings][settings_root]") {
    for (const char* row :
         {"row_touch_input", "row_printing", "row_safety", "row_system", "row_help"}) {
        CAPTURE(row);
        lv_obj_t* r = find(row);
        REQUIRE(r != nullptr);
        lv_obj_t* wrap = lv_obj_find_by_name(r, "status_wrap");
        REQUIRE(wrap != nullptr);
        CHECK(lv_obj_has_flag(wrap, LV_OBJ_FLAG_HIDDEN));
    }
}

TEST_CASE_METHOD(RootFixture,
                 "settings root: connection status resolves via the async Ethernet probe",
                 "[settings][settings_root]") {
    // Wi-Fi is mocked disconnected (WifiBackendMock starts with no SSID) and
    // EthernetBackendMock::get_info() always reports connected=true, so the
    // resolved status is deterministically "Ethernet". get_info_async() hands
    // the result back on an HttpExecutor worker thread, not synchronously, so
    // this can't be a plain process_lvgl(5) check.
    get_global_settings_panel().refresh_status_lines();
    REQUIRE(wait_until([&]() { return status_text(root_, "row_connection") == "Ethernet"; }));
}

TEST_CASE_METHOD(RootFixture, "settings root: Updates status reads firmware-managed",
                 "[settings][settings_root]") {
    set_int("updates_firmware_managed", 1);
    get_global_settings_panel().refresh_status_lines();
    process_lvgl(5);
    CHECK(status_text(root_, "row_updates") == "Managed by firmware");
}

TEST_CASE_METHOD(RootFixture, "settings root: refresh reads every stateful row's source",
                 "[settings][settings_root]") {
    set_int("settings_brightness", 65);
    set_int("settings_display_sleep", 600);
    set_int("settings_has_dimming", 1);
    set_int("settings_dark_mode", 1);
    set_int("settings_time_format", 1);

    lv_subject_t* hw_level = get_printer_state().get_hardware_status_level_subject();
    const int saved_hw_level = lv_subject_get_int(hw_level);
    lv_subject_set_int(hw_level, 1);

    get_global_settings_panel().refresh_status_lines();
    process_lvgl(5);

    CHECK(status_text(root_, "row_display") == "65% · sleep 10 min");
    CHECK(status_text(root_, "row_appearance") ==
          "Dark · " + DisplaySettingsManager::instance().get_theme_name());
    CHECK(status_text(root_, "row_devices") == "Needs attention");
    CHECK(status_text(root_, "row_language_time") == "English · 24-hour");

    lv_subject_set_int(hw_level, saved_hw_level);
}

TEST_CASE("SystemSettingsManager names the current language natively",
          "[settings][settings_root]") {
    CHECK_FALSE(helix::SystemSettingsManager::instance().get_language_display_name().empty());
}

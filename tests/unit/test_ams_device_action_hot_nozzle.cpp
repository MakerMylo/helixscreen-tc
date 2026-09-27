// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_device_action_hot_nozzle.cpp
 * @brief A device-action button that extrudes is refused on a cold nozzle.
 *
 * Run with: ./build/bin/helix-tests "[ams][hot_nozzle]"
 */

#include "ui_ams_device_section_detail_overlay.h"
#include "ui_nav_manager.h"
#include "ui_update_queue.h"

#include "../lvgl_ui_test_fixture.h"
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "moonraker_api.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"
#include "safety_settings_manager.h"
#include "static_panel_registry.h"

#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using helix::printer::ActionType;
using helix::printer::DeviceAction;
using helix::printer::DeviceSection;

namespace {

class HotNozzleActionFixture : public LVGLUITestFixture {
  public:
    HotNozzleActionFixture()
        : mock_client_(MoonrakerClientMock::PrinterType::VORON_24),
          api_(mock_client_, get_printer_state()) {
        // The overlay is a process-lifetime singleton whose widgets belong to
        // whichever test screen built it; drop any instance left behind.
        StaticPanelRegistry::instance().destroy_all();
        helix::ui::UpdateQueue::instance().drain();

        SafetyLimits limits; // min_extrude_temp_celsius = 170 (Klipper default)
        api_.set_safety_limits(limits);
        prev_api_ = get_moonraker_api();
        set_moonraker_api(&api_);
        prev_cold_extrude_ = helix::SafetySettingsManager::instance().get_allow_cold_extrude();
        helix::SafetySettingsManager::instance().set_allow_cold_extrude(false);

        helix::AmsState::instance().init_subjects(true);
        auto mock = std::make_unique<helix::AmsBackendMock>(4);
        mock_ = mock.get();
        mock_->set_device_sections({{"maintenance", "Maintenance", 0, "Maintenance"}});
        DeviceAction hot;
        hot.id = "load_extruder";
        hot.label = "Load Extruder";
        hot.section = "maintenance";
        hot.type = ActionType::BUTTON;
        hot.needs_hot_nozzle = true;
        DeviceAction cold = hot;
        cold.id = "test_grip";
        cold.label = "Test Grip";
        cold.needs_hot_nozzle = false;
        mock_->set_device_actions({hot, cold});
        helix::AmsState::instance().set_backend(std::move(mock));
        mock_->start();
    }

    ~HotNozzleActionFixture() override {
        NavigationManager::instance().go_back();
        helix::ui::UpdateQueue::instance().drain();
        process_lvgl(10);
        StaticPanelRegistry::instance().destroy_all();
        helix::ui::UpdateQueue::instance().drain();
        helix::AmsState::instance().set_backend(nullptr);
        set_moonraker_api(prev_api_);
        helix::SafetySettingsManager::instance().set_allow_cold_extrude(prev_cold_extrude_);
    }

    void set_nozzle_c(int celsius) {
        lv_subject_set_int(get_printer_state().get_active_extruder_temp_subject(), celsius * 10);
    }

    /// Click the named button in a freshly shown section overlay, and report
    /// which action reached the backend ("" = none).
    std::string click(const char* name) {
        auto& overlay = helix::ui::get_ams_device_section_detail_overlay();
        overlay.show(lv_screen_active(), "maintenance", "Maintenance");
        process_lvgl(20);
        lv_obj_t* btn = lv_obj_find_by_name(lv_screen_active(), name);
        REQUIRE(btn != nullptr);
        lv_obj_send_event(btn, LV_EVENT_CLICKED, nullptr);
        process_lvgl(20);
        return mock_->get_last_executed_action().first;
    }

    MoonrakerClientMock mock_client_;
    MoonrakerAPI api_;
    IMoonrakerAPI* prev_api_ = nullptr;
    helix::AmsBackendMock* mock_ = nullptr;
    bool prev_cold_extrude_ = false;
};

} // namespace

TEST_CASE_METHOD(HotNozzleActionFixture, "Device action needing heat is refused on a cold nozzle",
                 "[ams][hot_nozzle]") {
    set_nozzle_c(25);
    CHECK(click("load_extruder").empty());
}

TEST_CASE_METHOD(HotNozzleActionFixture, "Device action needing heat runs on a hot nozzle",
                 "[ams][hot_nozzle]") {
    set_nozzle_c(215);
    CHECK(click("load_extruder") == "load_extruder");
}

TEST_CASE_METHOD(HotNozzleActionFixture, "Device action needing no heat runs on a cold nozzle",
                 "[ams][hot_nozzle]") {
    set_nozzle_c(25);
    CHECK(click("test_grip") == "test_grip");
}

TEST_CASE_METHOD(HotNozzleActionFixture, "Cold-extrude opt-out lets a heat action through",
                 "[ams][hot_nozzle]") {
    set_nozzle_c(25);
    helix::SafetySettingsManager::instance().set_allow_cold_extrude(true);
    CHECK(click("load_extruder") == "load_extruder");
}

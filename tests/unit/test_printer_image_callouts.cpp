// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "panel_widget_manager.h"
#include "panel_widget_registry.h"
#include "src/ui/panel_widgets/printer_image_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE_METHOD(LVGLUITestFixture,
                 "printer image: callout layer and chips exist, hidden when idle",
                 "[printer_image][callouts]") {
    // Callout subjects (callout_nozzle_shown, printer_callout_mode, ...) must
    // exist before the XML parses, or every bind_flag_if(_eq) on them warns
    // and skips, leaving each chip at its unbound (visible) XML default.
    helix::init_widget_registrations();
    helix::PanelWidgetManager::instance().init_widget_subjects();

    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    REQUIRE(h.child("callout_layer"));
    for (const char* n : {"callout_chip_nozzle", "callout_chip_bed", "callout_chip_chamber",
                          "callout_chip_fan", "callout_chip_light", "callout_chip_toolhead"}) {
        INFO(n);
        REQUIRE(h.child(n));
        CHECK(lv_obj_has_flag(h.child(n), LV_OBJ_FLAG_HIDDEN));
    }
    CHECK(lv_obj_has_flag(h.child("callout_layer"), LV_OBJ_FLAG_IGNORE_LAYOUT));
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "printer image: every callout chip's clicked callback is registered",
                 "[printer_image][callouts]") {
    helix::init_widget_registrations();
    helix::PanelWidgetManager::instance().init_widget_subjects();

    CHECK(lv_xml_get_event_cb(nullptr, "printer_callout_nozzle_cb") ==
          PrinterImageWidget::printer_callout_nozzle_cb);
    CHECK(lv_xml_get_event_cb(nullptr, "printer_callout_bed_cb") ==
          PrinterImageWidget::printer_callout_bed_cb);
    CHECK(lv_xml_get_event_cb(nullptr, "printer_callout_chamber_cb") ==
          PrinterImageWidget::printer_callout_chamber_cb);
    CHECK(lv_xml_get_event_cb(nullptr, "printer_callout_fan_cb") ==
          PrinterImageWidget::printer_callout_fan_cb);
    CHECK(lv_xml_get_event_cb(nullptr, "printer_callout_light_cb") ==
          PrinterImageWidget::printer_callout_light_cb);
}

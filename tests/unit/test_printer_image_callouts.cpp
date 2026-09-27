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

TEST_CASE_METHOD(LVGLUITestFixture, "printer image: the light chip has no text label or subject",
                 "[printer_image][callouts]") {
    // callout_chip_light has no text to show: it borrows activity_chip's
    // styles.activity_chip look as a plain lv_obj holding only its icon,
    // rather than being an activity_chip instance with no text_subject.
    helix::init_widget_registrations();
    helix::PanelWidgetManager::instance().init_widget_subjects();

    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    lv_obj_t* light = h.child("callout_chip_light");
    REQUIRE(light);
    CHECK(lv_obj_find_by_name(light, "chip_text") == nullptr);
    // Still looks like a pill: same styles.activity_chip look every other
    // chip borrows (bg_opa and border_width are literals in that style, not
    // theme-token defaults a bare lv_obj would already carry).
    CHECK(lv_obj_get_style_bg_opa(light, LV_PART_MAIN) == 220);
    CHECK(lv_obj_get_style_border_width(light, LV_PART_MAIN) == 1);
}

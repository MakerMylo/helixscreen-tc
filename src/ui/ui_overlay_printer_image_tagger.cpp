// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_overlay_printer_image_tagger.h"

#include "ui_error_reporting.h"
#include "ui_event_safety.h"
#include "ui_nav_manager.h"

#include "lvgl/src/others/translation/lv_translation.h"
#include "panel_widgets/callout_layout.h"
#include "printer_image_manager.h"
#include "printer_images.h"
#include "static_panel_registry.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>

namespace helix::settings {

std::optional<ImageTagTarget> displayed_image_tag_target() {
    ImageTagTarget t;
    t.path = PrinterImageManager::instance().get_displayed_image_path(
        PrinterImages::current_screen_width());
    t.key = printer_image_region_key(t.path);
    lv_image_header_t hdr;
    if (t.key.empty() || lv_image_decoder_get_info(t.path.c_str(), &hdr) != LV_RESULT_OK)
        return std::nullopt;
    t.natural_w = static_cast<int>(hdr.w);
    t.natural_h = static_cast<int>(hdr.h);
    return t;
}

namespace {

const char* prompt_text(TagPrompt p) {
    switch (p) {
    case TagPrompt::Nozzle:
        return lv_tr("Tap the nozzle tip");
    case TagPrompt::PartFan:
        return lv_tr("Tap the part cooling fan");
    case TagPrompt::BedLeft:
        return lv_tr("Tap the bed's front-left corner");
    case TagPrompt::BedRight:
        return lv_tr("Tap the bed's front-right corner");
    case TagPrompt::Chamber:
        return lv_tr("Tap an empty spot inside the enclosure");
    case TagPrompt::Light:
        return lv_tr("Tap the light");
    case TagPrompt::Done:
        break;
    }
    return lv_tr("Check the chips, then save");
}

std::unique_ptr<PrinterImageTaggerOverlay> g_tagger_overlay;

} // namespace

PrinterImageTaggerOverlay& get_printer_image_tagger_overlay() {
    if (!g_tagger_overlay) {
        g_tagger_overlay = std::make_unique<PrinterImageTaggerOverlay>();
        StaticPanelRegistry::instance().register_destroy("PrinterImageTaggerOverlay",
                                                         []() { g_tagger_overlay.reset(); });
    }
    return *g_tagger_overlay;
}

PrinterImageTaggerOverlay::PrinterImageTaggerOverlay() = default;

PrinterImageTaggerOverlay::~PrinterImageTaggerOverlay() {
    if (subjects_initialized_) {
        deinit_subjects_base(subjects_);
    }
}

void PrinterImageTaggerOverlay::init_subjects() {
    if (subjects_initialized_) {
        return;
    }
    UI_MANAGED_SUBJECT_STRING(prompt_subject_, prompt_buf_, "", "printer_image_tagger_prompt",
                              subjects_);
    UI_MANAGED_SUBJECT_POINTER(image_src_subject_, image_src_buf_, "printer_image_tagger_src",
                               subjects_);
    UI_MANAGED_SUBJECT_INT(reviewing_subject_, 0, "printer_image_tagger_reviewing", subjects_);
    UI_MANAGED_SUBJECT_INT(can_skip_subject_, 0, "printer_image_tagger_can_skip", subjects_);
    UI_MANAGED_SUBJECT_INT(can_undo_subject_, 0, "printer_image_tagger_can_undo", subjects_);
    UI_MANAGED_SUBJECT_INT(fan_tagged_subject_, 0, "printer_image_tagger_fan", subjects_);
    UI_MANAGED_SUBJECT_INT(chamber_tagged_subject_, 0, "printer_image_tagger_chamber", subjects_);
    UI_MANAGED_SUBJECT_INT(light_tagged_subject_, 0, "printer_image_tagger_light", subjects_);
    subjects_initialized_ = true;
}

void PrinterImageTaggerOverlay::register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_printer_image_tagger_tap", on_tap);
    lv_xml_register_event_cb(nullptr, "on_printer_image_tagger_skip", on_skip);
    lv_xml_register_event_cb(nullptr, "on_printer_image_tagger_undo", on_undo);
    lv_xml_register_event_cb(nullptr, "on_printer_image_tagger_cancel", on_cancel);
    lv_xml_register_event_cb(nullptr, "on_printer_image_tagger_save", on_save);
}

lv_obj_t* PrinterImageTaggerOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        return overlay_root_;
    }
    overlay_root_ =
        static_cast<lv_obj_t*>(lv_xml_create(parent, "printer_image_tagger_overlay", nullptr));
    if (!overlay_root_) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }
    // Full screen: every pixel of image is room to tap.
    NavigationManager::instance().set_overlay_width_unmanaged(overlay_root_);
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);
    return overlay_root_;
}

void PrinterImageTaggerOverlay::show(lv_obj_t* parent_screen, const ImageTagTarget& target) {
    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }
    if (!overlay_root_ && parent_screen) {
        create(parent_screen);
    }
    if (!overlay_root_) {
        return;
    }
    target_ = target;
    session_ = {};
    std::strncpy(image_src_buf_, target_.path.c_str(), sizeof(image_src_buf_) - 1);
    lv_subject_set_pointer(&image_src_subject_, image_src_buf_);
    refresh();
    spdlog::info("[{}] Tagging '{}' ({}x{})", get_name(), target_.key, target_.natural_w,
                 target_.natural_h);

    NavigationManager::instance().register_overlay_instance(overlay_root_, this);
    NavigationManager::instance().push_overlay(overlay_root_);
}

void PrinterImageTaggerOverlay::on_activate() {
    OverlayBase::on_activate();
    refresh();
}

void PrinterImageTaggerOverlay::refresh() {
    const TagPrompt p = session_.prompt();
    const std::string text = session_.done()
                                 ? prompt_text(p)
                                 : fmt::format("{} ({}/{})", prompt_text(p), session_.step() + 1,
                                               ImageTagSession::PROMPT_COUNT);
    lv_subject_copy_string(&prompt_subject_, text.c_str());
    lv_subject_set_int(&reviewing_subject_, session_.done() ? 1 : 0);
    lv_subject_set_int(&can_skip_subject_, session_.can_skip() ? 1 : 0);
    lv_subject_set_int(&can_undo_subject_, session_.can_undo() ? 1 : 0);
    const ImageRegions r = session_.regions(target_.natural_w, target_.natural_h);
    lv_subject_set_int(&fan_tagged_subject_, r.part_fan ? 1 : 0);
    lv_subject_set_int(&chamber_tagged_subject_, r.chamber ? 1 : 0);
    lv_subject_set_int(&light_tagged_subject_, r.light ? 1 : 0);
    if (session_.done()) {
        place_review_chips();
    }
}

void PrinterImageTaggerOverlay::place_review_chips() {
    lv_obj_t* img = lv_obj_find_by_name(overlay_root_, "tagger_image");
    if (!img) {
        return;
    }
    const CalloutRect fit = fit_image(lv_obj_get_width(img), lv_obj_get_height(img),
                                      target_.natural_w, target_.natural_h);
    const ImageRegions r = session_.regions(target_.natural_w, target_.natural_h);
    const NormPoint bed{(r.bed_left.x + r.bed_right.x) / 2, (r.bed_left.y + r.bed_right.y) / 2};
    const std::pair<const char*, std::optional<NormPoint>> chips[] = {
        {"tagger_chip_nozzle", r.nozzle}, {"tagger_chip_fan", r.part_fan},
        {"tagger_chip_bed", bed},         {"tagger_chip_chamber", r.chamber},
        {"tagger_chip_light", r.light},
    };
    for (const auto& [name, pt] : chips) {
        lv_obj_t* chip = lv_obj_find_by_name(overlay_root_, name);
        if (chip && pt) {
            // Measured layout: the chip centres itself on this point in XML.
            lv_obj_set_pos(chip, callout_detail::px(pt->x, fit.x, fit.w),
                           callout_detail::px(pt->y, fit.y, fit.h));
        }
    }
}

void PrinterImageTaggerOverlay::handle_tap() {
    lv_indev_t* indev = lv_indev_active();
    lv_obj_t* img = overlay_root_ ? lv_obj_find_by_name(overlay_root_, "tagger_image") : nullptr;
    if (!indev || !img || session_.done()) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(img, &a);
    const CalloutRect fit = fit_image(lv_area_get_width(&a), lv_area_get_height(&a),
                                      target_.natural_w, target_.natural_h);
    const auto pt = image_point_at(fit, p.x - a.x1, p.y - a.y1);
    if (!pt) {
        return; // a tap in the letterbox, off the image
    }
    spdlog::debug("[{}] prompt {} -> ({:.3f}, {:.3f})", get_name(), session_.step(), pt->x, pt->y);
    session_.tap(*pt);
    refresh();
}

void PrinterImageTaggerOverlay::handle_save() {
    if (!session_.done()) {
        return;
    }
    if (!save_user_image_regions(target_.key,
                                 session_.regions(target_.natural_w, target_.natural_h))) {
        NOTIFY_ERROR(lv_tr("Could not save the printer image tags"));
        return;
    }
    spdlog::info("[{}] Saved tags for '{}'", get_name(), target_.key);
    PrinterImageManager::instance().notify_image_changed();
    NavigationManager::instance().go_back();
}

void PrinterImageTaggerOverlay::on_tap(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageTagger] on_tap");
    get_printer_image_tagger_overlay().handle_tap();
    LVGL_SAFE_EVENT_CB_END();
}

void PrinterImageTaggerOverlay::on_skip(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageTagger] on_skip");
    auto& self = get_printer_image_tagger_overlay();
    if (self.session_.skip()) {
        self.refresh();
    }
    LVGL_SAFE_EVENT_CB_END();
}

void PrinterImageTaggerOverlay::on_undo(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageTagger] on_undo");
    auto& self = get_printer_image_tagger_overlay();
    self.session_.undo();
    self.refresh();
    LVGL_SAFE_EVENT_CB_END();
}

void PrinterImageTaggerOverlay::on_cancel(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageTagger] on_cancel");
    NavigationManager::instance().go_back();
    LVGL_SAFE_EVENT_CB_END();
}

void PrinterImageTaggerOverlay::on_save(lv_event_t* /*e*/) {
    LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageTagger] on_save");
    get_printer_image_tagger_overlay().handle_save();
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::settings

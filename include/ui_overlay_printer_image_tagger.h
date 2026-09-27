// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "overlay_base.h"
#include "printer_image_regions.h"

#include <optional>
#include <string>

namespace helix::settings {

/// The image the home printer widget shows now, as the tagger sees it.
struct ImageTagTarget {
    std::string path; ///< LVGL path of the displayed image
    std::string key;  ///< printer_image_region_key(path)
    int natural_w = 0;
    int natural_h = 0;
};

/// The displayed image, or nullopt when it has no regions key or its size
/// cannot be read. The picker and the tagger both decide from this.
std::optional<ImageTagTarget> displayed_image_tag_target();

/**
 * @brief Full-screen overlay for tapping the parts of the displayed printer image
 *
 * One tap per ImageTagSession prompt, then a review step with chips at the
 * tapped points. Save stores the tags in the user regions file and redraws the
 * home widget; Cancel (or back) leaves without saving.
 */
class PrinterImageTaggerOverlay : public OverlayBase {
  public:
    PrinterImageTaggerOverlay();
    ~PrinterImageTaggerOverlay() override;

    void init_subjects() override;
    void register_callbacks() override;
    lv_obj_t* create(lv_obj_t* parent) override;

    const char* get_name() const override {
        return "Printer Image Tagger";
    }

    void on_activate() override;

    /// Start tagging `target`.
    void show(lv_obj_t* parent_screen, const ImageTagTarget& target);

  private:
    static void on_tap(lv_event_t* e);
    static void on_skip(lv_event_t* e);
    static void on_undo(lv_event_t* e);
    static void on_cancel(lv_event_t* e);
    static void on_save(lv_event_t* e);

    void handle_tap();
    void handle_save();
    /// Session state -> subjects; in review, moves each chip onto its point.
    void refresh();
    void place_review_chips();

    ImageTagTarget target_;
    ImageTagSession session_;

    SubjectManager subjects_;
    lv_subject_t prompt_subject_{};
    char prompt_buf_[160] = {};
    lv_subject_t image_src_subject_{};
    char image_src_buf_[512] = {};
    lv_subject_t reviewing_subject_{};
    lv_subject_t can_skip_subject_{};
    lv_subject_t can_undo_subject_{};
    lv_subject_t fan_tagged_subject_{};
    lv_subject_t chamber_tagged_subject_{};
    lv_subject_t light_tagged_subject_{};
};

PrinterImageTaggerOverlay& get_printer_image_tagger_overlay();

} // namespace helix::settings

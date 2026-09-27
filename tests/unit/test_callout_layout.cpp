// SPDX-License-Identifier: GPL-3.0-or-later
#include "src/ui/panel_widgets/callout_layout.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

// 800x480 2x2 widget: 160x160 container, K1C aspect (1601x1204).
CalloutLayoutInput base() {
    CalloutLayoutInput in;
    in.area_w = 160;
    in.area_h = 140;
    in.image_w = 1601;
    in.image_h = 1204;
    in.tagged = true;
    in.chip_h = 18;
    in.gap = 4;
    in.min_line = 8;
    return in;
}

const CalloutChipOut* find(const CalloutLayout& l, CalloutKind k) {
    for (const auto& c : l.chips)
        if (c.kind == k)
            return &c;
    return nullptr;
}

bool inside(const CalloutRect& r, int w, int h) {
    return r.x >= 0 && r.y >= 0 && r.x + r.w <= w && r.y + r.h <= h;
}

} // namespace

TEST_CASE("fit_image: contain-fit, centred", "[printer_image][callout_layout]") {
    const auto r = fit_image(200, 100, 400, 400);
    CHECK(r.w == 100);
    CHECK(r.h == 100);
    CHECK(r.x == 50);
    CHECK(r.y == 0);
    CHECK(fit_image(0, 100, 400, 400).w == 0);
    CHECK(fit_image(200, 100, 0, 0).w == 0);
}

TEST_CASE("spread_1d: resolves overlaps and stays within bounds",
          "[printer_image][callout_layout]") {
    std::vector<int> start{10, 12, 90};
    const std::vector<int> size{18, 18, 18};
    spread_1d(start, size, 0, 100, 4);
    CHECK(start[0] == 10);
    CHECK(start[1] == 32);
    CHECK(start[2] == 82); // pulled back up to fit the bottom edge
}

TEST_CASE("single cell shows the image only", "[printer_image][callout_layout]") {
    auto in = base();
    in.single_cell = true;
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}}};
    const auto l = compute_callout_layout(in);
    CHECK(l.mode == CalloutMode::ImageOnly);
    CHECK(l.chips.empty());
}

TEST_CASE("image shorter than three chips shows the image only (2x1 cells)",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 160;
    in.area_h = 3 * 18 - 1; // fitted K1C image is 53px tall
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}}};
    CHECK(compute_callout_layout(in).mode == CalloutMode::ImageOnly);
    in.area_h = 3 * 18;
    CHECK(compute_callout_layout(in).mode != CalloutMode::ImageOnly);
}

TEST_CASE("unlaid-out container shows the image only", "[printer_image][callout_layout]") {
    auto in = base();
    in.area_w = 0;
    in.area_h = 0;
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}}};
    CHECK(compute_callout_layout(in).mode == CalloutMode::ImageOnly);
}

TEST_CASE("pinned: chip centred on its point, inside the area", "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Bed, 60, NormPoint{0.46f, 0.57f}}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Pinned);
    const auto* bed = find(l, CalloutKind::Bed);
    REQUIRE(bed);
    const int ax = l.image.x + int(0.46f * l.image.w);
    const int ay = l.image.y + int(0.57f * l.image.h);
    CHECK(bed->rect.x + bed->rect.w / 2 == Catch::Approx(ax).margin(1));
    CHECK(bed->rect.y + bed->rect.h / 2 == Catch::Approx(ay).margin(1));
    CHECK_FALSE(bed->has_line);
    CHECK(inside(bed->rect, in.area_w, in.area_h));
}

TEST_CASE("pinned: a point at the image edge is clamped inside",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Light, 40, NormPoint{0.99f, 0.01f}}};
    const auto l = compute_callout_layout(in);
    CHECK(inside(find(l, CalloutKind::Light)->rect, in.area_w, in.area_h));
}

TEST_CASE("pinned: nozzle and fan merge into the toolhead chip",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Nozzle, 70, NormPoint{0.51f, 0.28f}},
                 {CalloutKind::Fan, 40, NormPoint{0.49f, 0.21f}}};
    in.toolhead = CalloutChipIn{CalloutKind::Toolhead, 110, NormPoint{0.51f, 0.28f}};
    const auto l = compute_callout_layout(in);
    CHECK(l.toolhead_merged);
    CHECK(find(l, CalloutKind::Toolhead));
    CHECK_FALSE(find(l, CalloutKind::Nozzle));
    CHECK_FALSE(find(l, CalloutKind::Fan));
}

TEST_CASE("pinned: nozzle alone does not merge", "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Nozzle, 70, NormPoint{0.51f, 0.28f}}};
    in.toolhead = CalloutChipIn{CalloutKind::Toolhead, 110, NormPoint{0.51f, 0.28f}};
    const auto l = compute_callout_layout(in);
    CHECK_FALSE(l.toolhead_merged);
    CHECK(find(l, CalloutKind::Nozzle));
}

TEST_CASE("tagged image, chip with no point is docked, not at the origin",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Nozzle, 60, NormPoint{0.5f, 0.3f}},
                 {CalloutKind::Chamber, 50, std::nullopt}};
    const auto l = compute_callout_layout(in);
    const auto* ch = find(l, CalloutKind::Chamber);
    REQUIRE(ch);
    CHECK(inside(ch->rect, in.area_w, in.area_h));
    CHECK(ch->rect.y + ch->rect.h == in.area_h - in.gap); // bottom dock row
}

TEST_CASE("untagged image: chips docked along the bottom, no lines, no overlap",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.tagged = false;
    in.active = {{CalloutKind::Nozzle, 60, std::nullopt},
                 {CalloutKind::Bed, 60, std::nullopt},
                 {CalloutKind::Fan, 40, std::nullopt}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Docked);
    REQUIRE(l.chips.size() == 3);
    for (size_t i = 0; i < l.chips.size(); ++i) {
        CHECK_FALSE(l.chips[i].has_line);
        CHECK(inside(l.chips[i].rect, in.area_w, in.area_h));
        for (size_t j = i + 1; j < l.chips.size(); ++j) {
            const auto& a = l.chips[i].rect;
            const auto& b = l.chips[j].rect;
            const bool overlap =
                a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
            CHECK_FALSE(overlap);
        }
    }
}

TEST_CASE("untagged image with a side band: chips stack in the band",
          "[printer_image][callout_layout]") {
    auto in = base();
    in.tagged = false;
    in.area_w = 330;
    in.area_h = 140; // image 186 wide, 72px band each side, chip column needs 68
    in.active = {{CalloutKind::Nozzle, 60, std::nullopt}, {CalloutKind::Bed, 60, std::nullopt}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::Docked);
    for (const auto& c : l.chips)
        CHECK(c.rect.x >= l.image.x + l.image.w);
}

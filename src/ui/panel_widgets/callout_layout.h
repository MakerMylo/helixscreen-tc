// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the printer image widget's live callouts: which
// mode the widget draws in, where the image sits, and where every chip and
// leader line goes. No LVGL, so every boundary is unit-testable. The widget
// measures chip widths in the fonts the chips render and feeds them here.

#include "printer_image_regions.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace helix {

enum class CalloutKind { Nozzle = 0, Bed = 1, Chamber = 2, Fan = 3, Light = 4, Toolhead = 5 };

/// The `printer_callout_mode` subject ints; XML ref_values read these.
enum class CalloutMode { ImageOnly = 0, Pinned = 1, Docked = 2, BothSides = 3, OneSide = 4 };

struct CalloutRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct CalloutChipIn {
    CalloutKind kind = CalloutKind::Nozzle;
    int w = 0;                       ///< measured width including padding and comfort margin
    std::optional<NormPoint> anchor; ///< tagged point; nullopt when the image has none for it
};

struct CalloutChipOut {
    CalloutKind kind = CalloutKind::Nozzle;
    CalloutRect rect;
    bool has_line = false;
    int line_x0 = 0, line_y0 = 0, line_x1 = 0, line_y1 = 0; ///< point -> chip edge
};

struct CalloutLayoutInput {
    int area_w = 0; ///< the image container's content box
    int area_h = 0;
    bool single_cell = false; ///< the grid granted one cell on both axes
    int image_w = 0;          ///< natural image size; only the aspect is used
    int image_h = 0;
    bool tagged = false; ///< the image has a regions entry
    int chip_h = 0;      ///< every chip is one text line tall
    int gap = 0;         ///< between stacked chips, and from the area edge
    int min_line = 0;    ///< shortest leader run worth drawing
    /// Worst-case chips this printer can ever show. Decides the MODE, so the
    /// image never moves when a chip comes or goes.
    std::vector<CalloutChipIn> budget;
    /// Chips showing now. Only these get positions.
    std::vector<CalloutChipIn> active;
    /// Nozzle+fan combined, for pinned mode when both are active.
    std::optional<CalloutChipIn> toolhead;
};

struct CalloutLayout {
    CalloutMode mode = CalloutMode::ImageOnly;
    CalloutRect image;
    std::vector<CalloutChipOut> chips;
    bool toolhead_merged = false;
};

/// Contain-fit the image into the area, centred. Zero rect when either is empty.
[[nodiscard]] inline CalloutRect fit_image(int area_w, int area_h, int img_w, int img_h) {
    if (area_w <= 0 || area_h <= 0 || img_w <= 0 || img_h <= 0)
        return {};
    CalloutRect r;
    if (int64_t(area_w) * img_h <= int64_t(area_h) * img_w) {
        r.w = area_w;
        r.h = int(int64_t(area_w) * img_h / img_w);
    } else {
        r.h = area_h;
        r.w = int(int64_t(area_h) * img_w / img_h);
    }
    r.x = (area_w - r.w) / 2;
    r.y = (area_h - r.h) / 2;
    return r;
}

/// Resolve overlaps along one axis. `start` holds each item's ideal start,
/// sorted ascending; items are pushed apart by `gap`, then pulled back so the
/// last ends by `hi`. The caller has already checked the items fit in [lo, hi].
inline void spread_1d(std::vector<int>& start, const std::vector<int>& size, int lo, int hi,
                      int gap) {
    for (size_t i = 0; i < start.size(); ++i) {
        const int min_s = i ? start[i - 1] + size[i - 1] + gap : lo;
        start[i] = std::max(start[i], min_s);
    }
    for (size_t i = start.size(); i-- > 0;) {
        const int max_s = (i + 1 < start.size() ? start[i + 1] - gap : hi) - size[i];
        start[i] = std::min(start[i], max_s);
    }
}

namespace callout_detail {

inline int px(float n, int origin, int extent) {
    return origin + int(n * float(extent));
}

inline CalloutRect clamp_into(CalloutRect r, int area_w, int area_h) {
    r.w = std::min(r.w, area_w);
    r.h = std::min(r.h, area_h);
    r.x = std::clamp(r.x, 0, std::max(0, area_w - r.w));
    r.y = std::clamp(r.y, 0, std::max(0, area_h - r.h));
    return r;
}

/// Chips with no usable point: a column in the widest side band if one fits
/// them, else a bottom-edge row filled right to left, wrapping upward.
inline void place_docked(const CalloutLayoutInput& in, const CalloutRect& img,
                         const std::vector<CalloutChipIn>& chips,
                         std::vector<CalloutChipOut>& out) {
    if (chips.empty())
        return;
    int widest = 0;
    for (const auto& c : chips)
        widest = std::max(widest, c.w);
    const int band_x = img.x + img.w;
    const int band_w = in.area_w - band_x;
    const int stack_h = int(chips.size()) * in.chip_h + int(chips.size() - 1) * in.gap;
    if (band_w >= widest + 2 * in.gap && stack_h <= in.area_h - 2 * in.gap) {
        int y = (in.area_h - stack_h) / 2;
        for (const auto& c : chips) {
            out.push_back({c.kind, {band_x + in.gap, y, c.w, in.chip_h}});
            y += in.chip_h + in.gap;
        }
        return;
    }
    int x = in.area_w - in.gap;
    int y = in.area_h - in.gap - in.chip_h;
    for (const auto& c : chips) {
        if (x - c.w < in.gap && x != in.area_w - in.gap) {
            x = in.area_w - in.gap;
            y -= in.chip_h + in.gap;
        }
        x -= c.w;
        out.push_back({c.kind, clamp_into({x, y, c.w, in.chip_h}, in.area_w, in.area_h)});
        x -= in.gap;
    }
}

} // namespace callout_detail

[[nodiscard]] inline CalloutLayout compute_callout_layout(const CalloutLayoutInput& in) {
    using namespace callout_detail;
    CalloutLayout out;
    out.image = fit_image(in.area_w, in.area_h, in.image_w, in.image_h);
    if (in.single_cell || out.image.w <= 0 || out.image.h < 3 * in.chip_h) {
        out.mode = CalloutMode::ImageOnly;
        return out;
    }

    std::vector<CalloutChipIn> chips = in.active;
    if (in.tagged) {
        // Phase 2 inserts the leader-line modes here, ahead of pinned.
        const auto has = [&](CalloutKind k) {
            return std::any_of(chips.begin(), chips.end(),
                               [k](const CalloutChipIn& c) { return c.kind == k; });
        };
        if (in.toolhead && has(CalloutKind::Nozzle) && has(CalloutKind::Fan)) {
            chips.erase(std::remove_if(chips.begin(), chips.end(),
                                       [](const CalloutChipIn& c) {
                                           return c.kind == CalloutKind::Nozzle ||
                                                  c.kind == CalloutKind::Fan;
                                       }),
                        chips.end());
            chips.push_back(*in.toolhead);
            out.toolhead_merged = true;
        }
        out.mode = CalloutMode::Pinned;
        std::vector<CalloutChipIn> unanchored;
        for (const auto& c : chips) {
            if (!c.anchor) {
                unanchored.push_back(c);
                continue;
            }
            const int cx = px(c.anchor->x, out.image.x, out.image.w);
            const int cy = px(c.anchor->y, out.image.y, out.image.h);
            out.chips.push_back(
                {c.kind, clamp_into({cx - c.w / 2, cy - in.chip_h / 2, c.w, in.chip_h}, in.area_w,
                                    in.area_h)});
        }
        // Unanchored chips always use the bottom row: the side bands belong to the image here.
        place_docked(in, CalloutRect{0, 0, in.area_w, 0}, unanchored, out.chips);
        return out;
    }

    out.mode = CalloutMode::Docked;
    place_docked(in, out.image, chips, out.chips);
    return out;
}

} // namespace helix

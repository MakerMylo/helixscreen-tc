// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace helix {

/// A point on a printer image, normalized 0..1 over the source PNG. The
/// prerendered tiers and the exact-size cache are aspect-preserving, centred
/// resizes of that PNG, so the same point holds at every rendered size.
struct NormPoint {
    float x = 0.f;
    float y = 0.f;
};

/// Hand-tagged parts of one printer image (assets/images/printers/regions.json).
struct ImageRegions {
    int src_w = 0; ///< source PNG width, guards against a re-cropped image
    int src_h = 0;
    NormPoint nozzle;
    NormPoint bed_left;  ///< near edge of the plate, left end as pictured
    NormPoint bed_right; ///< near edge of the plate, right end as pictured
    std::optional<NormPoint> part_fan;
    std::optional<NormPoint> chamber; ///< empty spot inside the enclosure
    std::optional<NormPoint> light;
};

/// Parse a regions document. Entries missing `size`, `nozzle` or `bed`, or
/// holding a malformed point, are skipped with a warning; a malformed
/// document yields an empty map.
std::unordered_map<std::string, ImageRegions> parse_image_regions(const std::string& json_text);

/// Regions for a shipped image basename ("creality-k1c"), or nullptr. The
/// shipped file is read once, on first call.
const ImageRegions* lookup_image_regions(std::string_view basename);

/// Replace what lookup_image_regions() reads. Tests only.
void set_image_regions_for_testing(std::unordered_map<std::string, ImageRegions> regions);

/// The regions key for an image path the printer image widget displays:
/// the file stem, minus a prerendered "-<size>" suffix. Empty for anything
/// outside the shipped printers directory (custom images carry no regions).
std::string printer_image_basename(std::string_view image_path);

} // namespace helix

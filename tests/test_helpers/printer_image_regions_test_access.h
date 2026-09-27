// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "printer_image_regions.h"

#include <string>
#include <unordered_map>

// Grants tests control of the table lookup_image_regions() reads. TestAccess
// pattern ([L088]) rather than a production _for_testing() entry; both are
// defined in src/system/printer_image_regions.cpp.
namespace helix {

/// Replace the table and mark it loaded, so the shipped file is never read.
void replace_image_regions(std::unordered_map<std::string, ImageRegions> regions);

/// Empty the table and mark it unloaded: the next lookup reads the shipped file.
void unload_image_regions();

/// Holds a replaced table for one scope, so a failing REQUIRE cannot leave a
/// test's tags behind for the next case.
class ScopedImageRegions {
  public:
    explicit ScopedImageRegions(std::unordered_map<std::string, ImageRegions> regions) {
        replace_image_regions(std::move(regions));
    }
    ~ScopedImageRegions() {
        unload_image_regions();
    }
    ScopedImageRegions(const ScopedImageRegions&) = delete;
    ScopedImageRegions& operator=(const ScopedImageRegions&) = delete;
};

} // namespace helix

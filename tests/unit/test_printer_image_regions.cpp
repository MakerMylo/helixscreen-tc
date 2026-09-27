// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_image_regions.h"

#include <cstdint>
#include <fstream>

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using namespace helix;

TEST_CASE("parse_image_regions: required, optional and malformed entries",
          "[printer_image][regions]") {
    const auto m = parse_image_regions(R"({
      "full":    {"size": [100, 50], "nozzle": [0.5, 0.2], "part_fan": [0.5, 0.1],
                  "chamber": [0.3, 0.4], "light": [0.2, 0.1], "bed": [[0.2, 0.7], [0.8, 0.7]]},
      "minimal": {"size": [10, 10], "nozzle": [0.5, 0.5], "bed": [[0.1, 0.9], [0.9, 0.9]]},
      "no_bed":  {"size": [10, 10], "nozzle": [0.5, 0.5]},
      "bad_pt":  {"size": [10, 10], "nozzle": "x", "bed": [[0.1, 0.9], [0.9, 0.9]]}
    })");
    REQUIRE(m.count("full") == 1);
    CHECK(m.at("full").src_w == 100);
    CHECK(m.at("full").part_fan.has_value());
    CHECK(m.at("full").bed_right.x == Catch::Approx(0.8f));
    REQUIRE(m.count("minimal") == 1);
    CHECK_FALSE(m.at("minimal").chamber.has_value());
    CHECK(m.count("no_bed") == 0);
    CHECK(m.count("bad_pt") == 0);
}

TEST_CASE("parse_image_regions: malformed document yields empty map", "[printer_image][regions]") {
    CHECK(parse_image_regions("{not json").empty());
    CHECK(parse_image_regions("[]").empty());
}

TEST_CASE("printer_image_basename: shipped paths resolve, custom paths do not",
          "[printer_image][regions]") {
    CHECK(printer_image_basename("A:assets/images/printers/prerendered/creality-k1c-300.bin") ==
          "creality-k1c");
    CHECK(printer_image_basename("A:assets/images/printers/prerendered/voron-v0-150.bin") ==
          "voron-v0");
    CHECK(printer_image_basename("A:assets/images/printers/creality-k1c.png") == "creality-k1c");
    CHECK(printer_image_basename("A:/assets/assets/images/printers/qidi-q2.png") == "qidi-q2");
    CHECK(printer_image_basename("A:/home/u/helixscreen/config/custom_images/creality-k1c-300.bin")
              .empty());
    CHECK(printer_image_basename("").empty());
}

TEST_CASE("lookup_image_regions: override map is what lookup reads", "[printer_image][regions]") {
    set_image_regions_for_testing({{"x", ImageRegions{}}});
    CHECK(lookup_image_regions("x") != nullptr);
    CHECK(lookup_image_regions("creality-k1c") == nullptr);
    set_image_regions_for_testing({});
}

TEST_CASE("lookup_image_regions: reads the shipped regions.json on first lookup",
          "[printer_image][regions]") {
    reset_image_regions_for_testing();
    const auto* regions = lookup_image_regions("creality-k1c");
    REQUIRE(regions != nullptr);
    CHECK(regions->src_w == 1601);
    reset_image_regions_for_testing();
}

// A re-cropped PNG silently shifts every tagged point; this names the image to re-tag.
TEST_CASE("regions.json: every entry's size matches its source PNG", "[printer_image][regions]") {
    std::ifstream f("assets/images/printers/regions.json");
    REQUIRE(f.good());
    const std::string text((std::istreambuf_iterator<char>(f)), {});
    const auto m = parse_image_regions(text);
    REQUIRE(m.size() >= 13);
    for (const auto& [name, r] : m) {
        std::ifstream png("assets/images/printers/" + name + ".png", std::ios::binary);
        INFO(name << ": PNG missing or re-sized; re-tag it with tools/printer-regions-tagger.html");
        REQUIRE(png.good());
        unsigned char hdr[24] = {};
        png.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
        const auto be32 = [&](int o) {
            return (uint32_t(hdr[o]) << 24) | (uint32_t(hdr[o + 1]) << 16) |
                   (uint32_t(hdr[o + 2]) << 8) | uint32_t(hdr[o + 3]);
        };
        CHECK(int(be32(16)) == r.src_w);
        CHECK(int(be32(20)) == r.src_h);
    }
}

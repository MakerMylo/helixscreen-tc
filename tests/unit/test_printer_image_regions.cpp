// SPDX-License-Identifier: GPL-3.0-or-later
#include "../test_helpers/config_dir_guard.h"
#include "../test_helpers/printer_image_regions_test_access.h"
#include "printer_image_regions.h"
#include "src/ui/panel_widgets/callout_layout.h"

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

TEST_CASE("printer_image_region_key: shipped by stem, custom by name, others empty",
          "[printer_image][regions]") {
    CHECK(printer_image_region_key("A:assets/images/printers/prerendered/creality-k1c-300.bin") ==
          "creality-k1c");
    CHECK(printer_image_region_key("A:assets/images/printers/prerendered/voron-v0-150.bin") ==
          "voron-v0");
    CHECK(printer_image_region_key("A:assets/images/printers/creality-k1c.png") == "creality-k1c");
    CHECK(printer_image_region_key("A:/assets/assets/images/printers/qidi-q2.png") == "qidi-q2");
    CHECK(printer_image_region_key("A:assets/images/printers/voron-0-2.png") == "voron-0-2");
    CHECK(printer_image_region_key(
              "A:/home/u/helixscreen/config/custom_images/creality-k1c-300.bin") ==
          "custom:creality-k1c");
    CHECK(printer_image_region_key("A:config/custom_images/my-printer-2-150.bin") ==
          "custom:my-printer-2");
    CHECK(printer_image_region_key("A:/srv/elsewhere/printer-300.bin").empty());
    CHECK(printer_image_region_key("").empty());
}

TEST_CASE("lookup_image_regions: override map is what lookup reads", "[printer_image][regions]") {
    const ConfigDirGuard cfg("regions_override_map");
    const ScopedImageRegions regions({{"x", ImageRegions{}}});
    CHECK(lookup_image_regions("x", 0, 0) != nullptr);
    CHECK(lookup_image_regions("creality-k1c", 0, 0) == nullptr);
}

TEST_CASE("lookup_image_regions: reads the shipped regions.json on first lookup",
          "[printer_image][regions]") {
    const ConfigDirGuard cfg("regions_shipped_read");
    unload_image_regions();
    const auto* regions = lookup_image_regions("creality-k1c", 1601, 1204);
    REQUIRE(regions != nullptr);
    CHECK(regions->src_w == 1601);
    unload_image_regions();
}

namespace {

ImageRegions shipped_entry(float nozzle_x) {
    ImageRegions r;
    r.src_w = 1600;
    r.src_h = 800;
    r.nozzle = {nozzle_x, 0.2f};
    r.bed_left = {0.2f, 0.8f};
    r.bed_right = {0.8f, 0.8f};
    return r;
}

void write_user_file(const ConfigDirGuard& cfg, const std::string& text) {
    std::ofstream(cfg.dir / "printer_image_regions.json") << text;
}

std::string read_user_file(const ConfigDirGuard& cfg) {
    std::ifstream f(cfg.dir / "printer_image_regions.json");
    return std::string((std::istreambuf_iterator<char>(f)), {});
}

} // namespace

TEST_CASE("user regions: a user entry overrides the shipped entry for its key only",
          "[printer_image][regions][image_tagger]") {
    const ConfigDirGuard cfg("regions_user_override");
    write_user_file(cfg, R"({
      "voron-v2": {"size": [300, 150], "nozzle": [0.9, 0.1], "bed": [[0.1, 0.9], [0.9, 0.9]]},
      "custom:mine": {"size": [300, 200], "nozzle": [0.4, 0.3], "bed": [[0.1, 0.9], [0.9, 0.9]]}
    })");
    const ScopedImageRegions shipped(
        {{"voron-v2", shipped_entry(0.5f)}, {"creality-k1c", shipped_entry(0.6f)}});

    const auto* v2 = lookup_image_regions("voron-v2", 300, 150);
    REQUIRE(v2 != nullptr);
    CHECK(v2->nozzle.x == Catch::Approx(0.9f));
    const auto* k1c = lookup_image_regions("creality-k1c", 300, 225);
    REQUIRE(k1c != nullptr);
    CHECK(k1c->nozzle.x == Catch::Approx(0.6f));
    const auto* mine = lookup_image_regions("custom:mine", 300, 200);
    REQUIRE(mine != nullptr);
    CHECK(mine->nozzle.x == Catch::Approx(0.4f));
    CHECK(lookup_image_regions("custom:other", 300, 200) == nullptr);
    CHECK(has_shipped_image_regions("voron-v2"));
    CHECK_FALSE(has_shipped_image_regions("custom:mine"));
}

TEST_CASE("user regions: an entry tagged on an image of another size is ignored",
          "[printer_image][regions][image_tagger]") {
    const ConfigDirGuard cfg("regions_user_size_guard");
    write_user_file(cfg, R"({
      "voron-v2": {"size": [300, 150], "nozzle": [0.9, 0.1], "bed": [[0.1, 0.9], [0.9, 0.9]]},
      "custom:mine": {"size": [300, 200], "nozzle": [0.4, 0.3], "bed": [[0.1, 0.9], [0.9, 0.9]]}
    })");
    const ScopedImageRegions shipped({{"voron-v2", shipped_entry(0.5f)}});

    // Falls back to the shipped entry...
    const auto* v2 = lookup_image_regions("voron-v2", 300, 160);
    REQUIRE(v2 != nullptr);
    CHECK(v2->nozzle.x == Catch::Approx(0.5f));
    CHECK(lookup_user_image_regions("voron-v2", 300, 160) == nullptr);
    // ...or to untagged when there is none.
    CHECK(lookup_image_regions("custom:mine", 200, 300) == nullptr);
    CHECK(lookup_user_image_regions("custom:mine", 300, 200) != nullptr);
}

TEST_CASE("image_point_at: taps map through a contain-fit, letterboxed on either axis",
          "[printer_image][image_tagger]") {
    // Wide area, tall-ish image: bars left and right.
    const auto pillar = fit_image(400, 200, 100, 100); // 200x200 at x=100
    REQUIRE(pillar.x == 100);
    const auto p = image_point_at(pillar, 150, 50);
    REQUIRE(p);
    CHECK(p->x == Catch::Approx(0.25f));
    CHECK(p->y == Catch::Approx(0.25f));
    CHECK_FALSE(image_point_at(pillar, 99, 50));
    CHECK_FALSE(image_point_at(pillar, 300, 50));

    // Tall area, wide image: bars above and below.
    const auto letter = fit_image(200, 400, 200, 100); // 200x100 at y=150
    REQUIRE(letter.y == 150);
    const auto q = image_point_at(letter, 100, 175);
    REQUIRE(q);
    CHECK(q->x == Catch::Approx(0.5f));
    CHECK(q->y == Catch::Approx(0.25f));
    CHECK_FALSE(image_point_at(letter, 100, 149));
    CHECK_FALSE(image_point_at(letter, 100, 250));
}

TEST_CASE("ImageTagSession: prompt order, skips, undo", "[printer_image][image_tagger]") {
    ImageTagSession s;
    CHECK(s.prompt() == TagPrompt::Nozzle);
    CHECK_FALSE(s.can_undo());
    CHECK_FALSE(s.can_skip());
    CHECK_FALSE(s.skip()); // the nozzle is required
    CHECK(s.prompt() == TagPrompt::Nozzle);

    s.tap({0.5f, 0.2f});
    CHECK(s.prompt() == TagPrompt::PartFan);
    CHECK(s.can_skip());
    CHECK(s.skip());
    CHECK(s.prompt() == TagPrompt::BedLeft);
    CHECK_FALSE(s.skip()); // both bed ends are required
    s.tap({0.2f, 0.8f});
    CHECK(s.prompt() == TagPrompt::BedRight);
    CHECK_FALSE(s.skip());
    s.tap({0.8f, 0.8f});
    CHECK(s.prompt() == TagPrompt::Chamber);

    // Undo steps back one prompt and forgets its point.
    s.undo();
    CHECK(s.prompt() == TagPrompt::BedRight);
    s.tap({0.7f, 0.7f});
    s.tap({0.3f, 0.4f});
    CHECK(s.prompt() == TagPrompt::Light);
    CHECK(s.skip());
    CHECK(s.done());
    CHECK(s.prompt() == TagPrompt::Done);
    CHECK_FALSE(s.can_skip());

    const auto r = s.regions(300, 200);
    CHECK(r.src_w == 300);
    CHECK(r.src_h == 200);
    CHECK(r.nozzle.x == Catch::Approx(0.5f));
    CHECK(r.bed_right.x == Catch::Approx(0.7f));
    CHECK_FALSE(r.part_fan.has_value());
    REQUIRE(r.chamber.has_value());
    CHECK(r.chamber->y == Catch::Approx(0.4f));
    CHECK_FALSE(r.light.has_value());

    // Undo from the end, past a skip, returns to that prompt.
    s.undo();
    CHECK(s.prompt() == TagPrompt::Light);
    s.tap({0.1f, 0.1f});
    CHECK(s.regions(300, 200).light.has_value());
}

TEST_CASE("user regions: save writes the entry, reset removes only its key",
          "[printer_image][regions][image_tagger]") {
    const ConfigDirGuard cfg("regions_user_save");
    const ScopedImageRegions shipped({{"voron-v2", shipped_entry(0.5f)}});

    ImageTagSession s;
    s.tap({0.5f, 0.25f});
    s.tap({0.5f, 0.125f});
    s.tap({0.25f, 0.75f});
    s.tap({0.75f, 0.75f});
    s.skip();
    s.tap({0.375f, 0.125f});
    REQUIRE(s.done());
    REQUIRE(save_user_image_regions("custom:mine", s.regions(300, 200)));
    REQUIRE(save_user_image_regions("voron-v2", s.regions(300, 150)));

    const auto doc = nlohmann::json::parse(read_user_file(cfg));
    CHECK(doc["custom:mine"] == nlohmann::json::parse(R"({"size": [300, 200], "nozzle": [0.5, 0.25],
                                    "part_fan": [0.5, 0.125], "light": [0.375, 0.125],
                                    "bed": [[0.25, 0.75], [0.75, 0.75]]})"));
    // The file reads back through the same parser as regions.json.
    CHECK(parse_image_regions(read_user_file(cfg)).size() == 2);
    // The saved tags take effect without a reload.
    const auto* v2 = lookup_image_regions("voron-v2", 300, 150);
    REQUIRE(v2 != nullptr);
    CHECK(v2->part_fan.has_value());

    REQUIRE(reset_user_image_regions("voron-v2"));
    const auto after = nlohmann::json::parse(read_user_file(cfg));
    CHECK_FALSE(after.contains("voron-v2"));
    CHECK(after.contains("custom:mine"));
    // Back to the shipped points.
    v2 = lookup_image_regions("voron-v2", 300, 150);
    REQUIRE(v2 != nullptr);
    CHECK(v2->nozzle.x == Catch::Approx(0.5f));
    CHECK_FALSE(v2->part_fan.has_value());
    CHECK(lookup_image_regions("custom:mine", 300, 200) != nullptr);

    // A fresh load reads the same state back from disk.
    unload_image_regions();
    CHECK(lookup_user_image_regions("voron-v2", 300, 150) == nullptr);
    CHECK(lookup_user_image_regions("custom:mine", 300, 200) != nullptr);
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

TEST_CASE("review_callout_layout: close nozzle and fan points give chips that do not overlap",
          "[printer_image][image_tagger]") {
    ImageRegions r;
    r.src_w = 750;
    r.src_h = 930;
    r.nozzle = {0.45f, 0.61f};
    r.part_fan = NormPoint{0.45f, 0.58f};
    r.bed_left = {0.2f, 0.66f};
    r.bed_right = {0.55f, 0.70f};
    r.chamber = NormPoint{0.3f, 0.36f};
    r.light = NormPoint{0.8f, 0.16f};
    CalloutChipWidths w{};
    w.fill(75);
    w[size_t(CalloutKind::Toolhead)] = 120;

    const auto out = review_callout_layout(r, 786, 326, w, 30, 4);
    REQUIRE(out.mode == CalloutMode::Pinned);
    REQUIRE(out.chips.size() >= 4);
    for (size_t i = 0; i < out.chips.size(); ++i) {
        for (size_t j = i + 1; j < out.chips.size(); ++j) {
            const auto& a = out.chips[i].rect;
            const auto& b = out.chips[j].rect;
            INFO("chips " << int(out.chips[i].kind) << " and " << int(out.chips[j].kind));
            CHECK((a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y));
        }
    }
}

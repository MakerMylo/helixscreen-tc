# Printer Image Live Callouts Implementation Plan (phases 1-2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Live temperature, fan and light chips on the home-panel printer image: pinned to
hand-tagged points, docked on untagged images, with leader lines when the widget leaves a free
band beside the image.

**Architecture:** The measure / decide / publish / bind pattern from
`docs/devel/PANEL_WIDGET_GUIDE.md`. A header-only pure function (`compute_callout_layout()`)
decides mode and every chip/line position from measured pixels. `PrinterImageWidget` observes
the existing printer subjects, classifies and formats with the existing temperature helpers,
publishes chip text/visibility/mode as subjects, and applies positions from a deferred timer.
XML binds all visibility; only geometry is set from C++.

**Tech Stack:** C++17, LVGL 9.5, helix-xml components, Catch2 (`tests/unit/`), `hv/json.hpp`.

**Spec:** `docs/devel/plans/2026-09-26-printer-image-callouts-design.md`. Read it first.
Phase 3 (in-app tagger) gets its own plan once phases 1-2 ship.

## Global Constraints

- Work only in `.worktrees/1397-printer-image-callouts` (branch `feature/1397-printer-image-callouts`). Hold `worktree:1397-printer-image-callouts` via `scripts/helix-claim` while editing.
- Temperatures are decidegrees (`int`, 10 = 1°C) everywhere.
- Chip temperature text is `helix::ui::temperature::heater_display(current_deci, target_deci).temp` verbatim (`"205 / 220°C"` while a target is set, `"64°C"` when not). Do not write another temperature formatter.
- Fan text is `"%d%%"`, the format `FanWidget` uses (`src/ui/panel_widgets/fan_widget.cpp`).
- Residual heat threshold: 50°C (`RESIDUAL_HEAT_THRESHOLD_DECI = 500`).
- Heater chip icons are tinted and pulsed by `HeaterIconBinder`, never styled from C++. Chip text keeps the normal text colour.
- Declarative UI rules (`.claude/rules/declarative-ui.md`): no `lv_obj_add_event_cb`, no `lv_obj_add_flag(HIDDEN)`, no `lv_label_set_text`, no C++ styling. Compound visibility goes in XML `cond`. Chip x/y, size and line points are measured layout: mark each such call `// DECLARATIVE_OK: measured callout layout`.
- Design tokens only (`#space_*`, `text_small`, theme colours). `scripts/check_hardcoded_pixels.py` ratchets raw pixel literals.
- Never force layout: no `lv_obj_update_layout()`, and all callout geometry is applied from a deferred one-shot timer, like `schedule_cache_check()` (#983, #1025).
- Comments state constraints, never history (CLAUDE.md § Comments describe the code).
- Commit with explicit pathspecs; `make mutate-diff` before each commit that adds tests, and name the mutation in the commit body.
- `regions.json` keys are image basenames; points are normalized 0..1 over the source PNG.

## Review Focus

1. **An active chip whose part has no tagged point** (chamber heater running on an image tagged without `chamber`; light on with no `light` point): the chip is docked, never drawn at (0,0) or hidden. Test in Task 3.
2. **A short, wide widget** (2×1 cells, ~160×80 on 800×480): the fitted image is too short to host chips, so the widget shows the image only, not chips piled on each other. Rule: image-only when the fitted image is shorter than 3 chip heights. Test in Task 3.
3. **Unlaid-out or unresolvable image** (container 0×0 during a rebuild, or `lv_image_decoder_get_info` fails on a custom image): no crash, callouts stay hidden, and the next deferred pass fills them in. A tagged image takes its aspect from `regions.json` `size`, so only untagged images depend on the decoder. Tests in Tasks 3 and 5.
4. **Chips coming and going must not move the image**: the mode is decided from the worst-case chip set the printer can show, not from the chips active right now, so the image never jumps sideways when a fan starts. Test in Task 6.
5. **Recycled widget instance** (the home panel rebuilds and re-`attach()`es the same `PrinterImageWidget`): observers re-arm, the callout timer is cancelled in `detach()`, and callouts re-apply from `attach()`. Test in Task 5.

---

## File Map

| File | Status | Responsibility |
|---|---|---|
| `include/ui_temperature_utils.h`, `src/ui/ui_temperature_utils.cpp` | modify | `RESIDUAL_HEAT_THRESHOLD_DECI`, `is_residual_hot()` |
| `include/printer_image_regions.h`, `src/system/printer_image_regions.cpp` | create | parse/load `regions.json`, `lookup()`, `printer_image_basename()` |
| `assets/images/printers/regions.json` | exists | tagged points (13 images) |
| `assets/images/printers/README.md` | rewrite | image + regions authoring guide |
| `scripts/esp32_stage_assets.py` | modify | stage `regions.json` |
| `src/ui/panel_widgets/text_measure.h` | create | the one `measure_text_px()` |
| `src/ui/panel_widgets/nozzle_temps_widget.cpp`, `fan_stack_widget.cpp` | modify | use `text_measure.h` |
| `src/ui/panel_widgets/callout_layout.h` | create | pure `compute_callout_layout()` |
| `ui_xml/components/activity_chip.xml` | create | icon slot + bound text pill |
| `src/xml_registration.cpp` | modify | register `activity_chip.xml` |
| `ui_xml/components/panel_widget_printer_image.xml` | modify | callout layer: chips, lines, glow |
| `src/ui/panel_widgets/printer_image_widget.{h,cpp}` | modify | subjects, observers, measure, apply, taps |
| `tests/unit/test_residual_heat.cpp` | create | `[temperature][residual]` |
| `tests/unit/test_printer_image_regions.cpp` | create | `[printer_image][regions]` |
| `tests/unit/test_callout_layout.cpp` | create | `[printer_image][callout_layout]` |
| `tests/unit/test_printer_image_callouts.cpp` | create | `[printer_image][callouts]` |
| `scripts/screenshot-recipes.sh` | modify | callout demo tokens |

New `src/*/*.cpp` and `tests/unit/*.cpp` files are picked up by the Makefile wildcards; no build edits.

---

### Task 1: Residual heat

**Files:**
- Modify: `include/ui_temperature_utils.h` (beside `classify_heat_state`), `src/ui/ui_temperature_utils.cpp`
- Test: `tests/unit/test_residual_heat.cpp`

**Interfaces:**
- Produces: `helix::ui::temperature::RESIDUAL_HEAT_THRESHOLD_DECI` (`constexpr int`, 500) and `bool helix::ui::temperature::is_residual_hot(int current_deci)`.

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_temperature_utils.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui::temperature;

TEST_CASE("is_residual_hot: hot above the threshold, cool at or below it",
          "[temperature][residual]") {
    CHECK(is_residual_hot(RESIDUAL_HEAT_THRESHOLD_DECI + 1));
    CHECK(is_residual_hot(2100));
    CHECK_FALSE(is_residual_hot(RESIDUAL_HEAT_THRESHOLD_DECI));
    CHECK_FALSE(is_residual_hot(250));
    CHECK_FALSE(is_residual_hot(0));
}

TEST_CASE("is_residual_hot: threshold is 50C in decidegrees", "[temperature][residual]") {
    CHECK(RESIDUAL_HEAT_THRESHOLD_DECI == 500);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[residual]'`
Expected: compile error, `is_residual_hot` / `RESIDUAL_HEAT_THRESHOLD_DECI` not declared.

- [ ] **Step 3: Implement**

In `include/ui_temperature_utils.h`, after the `classify_heat_state_with_mode` declaration:

```cpp
/// A heater whose target is off but whose part is still hot enough to burn.
/// Callers keep showing it (muted) until it cools below this.
constexpr int RESIDUAL_HEAT_THRESHOLD_DECI = 500;

/// True while `current_deci` is above the residual-heat threshold. Independent
/// of the target: callers decide whether an "on" heater counts.
bool is_residual_hot(int current_deci);
```

In `src/ui/ui_temperature_utils.cpp`, beside `classify_heat_state`:

```cpp
bool is_residual_hot(int current_deci) {
    return current_deci > RESIDUAL_HEAT_THRESHOLD_DECI;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `make t F='[residual]'` → PASS (2 test cases).

- [ ] **Step 5: Mutate and commit**

Run: `make mutate-diff`. Expected: flipping `>` to `>=` turns `[residual]` red.

```bash
git add tests/unit/test_residual_heat.cpp
git commit -m "feat(temperature): residual-heat threshold for heaters that are off but still hot (prestonbrown/helixscreen#1397)

Mutation: > to >= in is_residual_hot fails [residual]." -- include/ui_temperature_utils.h src/ui/ui_temperature_utils.cpp tests/unit/test_residual_heat.cpp
```

---

### Task 2: Regions data, loader and guard

**Files:**
- Create: `include/printer_image_regions.h`, `src/system/printer_image_regions.cpp`
- Rewrite: `assets/images/printers/README.md`
- Modify: `scripts/esp32_stage_assets.py#stage_printer_images`
- Test: `tests/unit/test_printer_image_regions.cpp`

**Interfaces:**
- Produces:
  - `struct helix::NormPoint { float x; float y; }`
  - `struct helix::ImageRegions { int src_w; int src_h; NormPoint nozzle; NormPoint bed_left; NormPoint bed_right; std::optional<NormPoint> part_fan, chamber, light; }`
  - `std::unordered_map<std::string, ImageRegions> helix::parse_image_regions(const std::string& json_text)`
  - `const ImageRegions* helix::lookup_image_regions(std::string_view basename)` (lazy-loads the shipped file once)
  - `void helix::set_image_regions_for_testing(std::unordered_map<std::string, ImageRegions>)`
  - `std::string helix::printer_image_basename(std::string_view image_path)`

- [ ] **Step 1: Write the failing tests**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_image_regions.h"

#include "hv/json.hpp"

#include <cstdint>
#include <fstream>

#include "../catch_amalgamated.hpp"

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
```

The last case reads files relative to the repo root, which is the test binary's working
directory (see how `tests/unit/test_printer_image_manager.cpp` opens `assets/`). If it is not,
resolve through `helix::asset_path()` as the loader does.

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[regions]'` → compile error, header missing.

- [ ] **Step 3: Implement the header**

```cpp
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
```

- [ ] **Step 4: Implement the source**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "printer_image_regions.h"

#include "data_root_resolver.h"
#include "hv/json.hpp"

#include <spdlog/spdlog.h>

#include <cctype>
#include <fstream>

namespace helix {

namespace {

std::optional<NormPoint> read_point(const nlohmann::json& v) {
    if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number())
        return std::nullopt;
    return NormPoint{v[0].get<float>(), v[1].get<float>()};
}

std::unordered_map<std::string, ImageRegions>& table() {
    static std::unordered_map<std::string, ImageRegions> t;
    return t;
}

bool& loaded() {
    static bool l = false;
    return l;
}

} // namespace

std::unordered_map<std::string, ImageRegions> parse_image_regions(const std::string& json_text) {
    std::unordered_map<std::string, ImageRegions> out;
    const auto doc = nlohmann::json::parse(json_text, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object()) {
        spdlog::warn("[PrinterImageRegions] regions document is not a JSON object");
        return out;
    }
    for (const auto& [name, e] : doc.items()) {
        const auto size = e.is_object() && e.contains("size") ? read_point(e["size"]) : std::nullopt;
        const auto nozzle = e.is_object() && e.contains("nozzle") ? read_point(e["nozzle"]) : std::nullopt;
        const auto& bed = e.is_object() && e.contains("bed") ? e["bed"] : nlohmann::json();
        const auto bl = bed.is_array() && bed.size() == 2 ? read_point(bed[0]) : std::nullopt;
        const auto br = bed.is_array() && bed.size() == 2 ? read_point(bed[1]) : std::nullopt;
        if (!size || !nozzle || !bl || !br) {
            spdlog::warn("[PrinterImageRegions] skipping '{}': needs size, nozzle and bed", name);
            continue;
        }
        ImageRegions r;
        r.src_w = static_cast<int>(size->x);
        r.src_h = static_cast<int>(size->y);
        r.nozzle = *nozzle;
        r.bed_left = *bl;
        r.bed_right = *br;
        if (e.contains("part_fan"))
            r.part_fan = read_point(e["part_fan"]);
        if (e.contains("chamber"))
            r.chamber = read_point(e["chamber"]);
        if (e.contains("light"))
            r.light = read_point(e["light"]);
        out.emplace(name, r);
    }
    return out;
}

const ImageRegions* lookup_image_regions(std::string_view basename) {
    if (!loaded()) {
        loaded() = true;
        std::ifstream f(asset_path("assets/images/printers/regions.json"));
        if (f.good()) {
            const std::string text((std::istreambuf_iterator<char>(f)), {});
            table() = parse_image_regions(text);
            spdlog::debug("[PrinterImageRegions] {} tagged images", table().size());
        } else {
            spdlog::debug("[PrinterImageRegions] no regions.json; every image is untagged");
        }
    }
    const auto it = table().find(std::string(basename));
    return it == table().end() ? nullptr : &it->second;
}

void set_image_regions_for_testing(std::unordered_map<std::string, ImageRegions> regions) {
    table() = std::move(regions);
    loaded() = true;
}

std::string printer_image_basename(std::string_view path) {
    // Custom images live under the config dir's custom_images/, never here.
    if (path.find("/images/printers/") == std::string_view::npos)
        return {};
    const auto slash = path.find_last_of('/');
    std::string_view file = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const auto dot = file.find_last_of('.');
    std::string stem(file.substr(0, dot));
    if (path.find("/prerendered/") != std::string_view::npos) {
        const auto dash = stem.find_last_of('-');
        if (dash != std::string::npos && dash + 1 < stem.size() &&
            std::all_of(stem.begin() + dash + 1, stem.end(),
                        [](unsigned char c) { return std::isdigit(c); }))
            stem.resize(dash);
    }
    return stem;
}

} // namespace helix
```

Add `#include <algorithm>`. If `get_best_printer_image()` returns a spelling without `/images/printers/` on some platform,
add it to the test first.

- [ ] **Step 5: Run to verify it passes**

Run: `make t F='[regions]'` → PASS (5 test cases).

- [ ] **Step 6: Ship the data everywhere the images go**

In `scripts/esp32_stage_assets.py#stage_printer_images`, after the `copytree`, add:

```python
    regions = repo_root / "assets" / "images" / "printers" / "regions.json"
    if regions.is_file():
        shutil.copy2(regions, dest / "regions.json")
```

Run `python3 scripts/check_platform_manifest.py` and follow what it reports for the Android
asset list; if it names nothing, the wholesale `assets/` copy already covers it.

- [ ] **Step 7: Rewrite `assets/images/printers/README.md`**

Replace the stale 13-image table with: how an image is chosen (`printer_database.json` `image`
field; custom images from Printer Manager), source PNG expectations (transparent background,
no manual cropping after tagging), the prerender pipeline pointer
(`docs/devel/PRE_RENDERED_IMAGES.md`), and a "Tagging parts for live callouts" section: the
`regions.json` format (copy the table from the spec's Data section), the tagger commands
(`python3 -m http.server -d . 8000`, then `/tools/printer-regions-tagger.html`), and the rule
that re-cropping a tagged PNG requires re-tagging (the `[regions]` size test enforces it).

- [ ] **Step 8: Mutate and commit**

Run: `make mutate-diff`. Expected: removing the `/images/printers/` early return, or the
`-<size>` strip, fails `[regions]`.

```bash
git add include/printer_image_regions.h src/system/printer_image_regions.cpp tests/unit/test_printer_image_regions.cpp
git commit -m "feat(printer-image): load hand-tagged image regions, guard them against re-cropped PNGs (prestonbrown/helixscreen#1397)

Mutation: dropping the printers-dir early return fails [regions]." -- include/printer_image_regions.h src/system/printer_image_regions.cpp tests/unit/test_printer_image_regions.cpp assets/images/printers/README.md scripts/esp32_stage_assets.py
```

---

### Task 3: Pure layout, phase 1 modes (image only, pinned, docked)

**Files:**
- Create: `src/ui/panel_widgets/callout_layout.h`
- Test: `tests/unit/test_callout_layout.cpp`

**Interfaces:**
- Consumes: `helix::NormPoint` (Task 2).
- Produces (all `namespace helix`, header-only `inline`):
  - `enum class CalloutKind { Nozzle = 0, Bed = 1, Chamber = 2, Fan = 3, Light = 4, Toolhead = 5 };`
  - `enum class CalloutMode { ImageOnly = 0, Pinned = 1, Docked = 2, BothSides = 3, OneSide = 4 };` (ints are the `printer_callout_mode` subject values)
  - `struct CalloutRect { int x, y, w, h; };`
  - `struct CalloutChipIn { CalloutKind kind; int w; std::optional<NormPoint> anchor; };`
  - `struct CalloutChipOut { CalloutKind kind; CalloutRect rect; bool has_line; int line_x0, line_y0, line_x1, line_y1; };`
  - `struct CalloutLayoutInput { int area_w, area_h; bool single_cell; int image_w, image_h; bool tagged; int chip_h, gap, min_line; std::vector<CalloutChipIn> budget, active; std::optional<CalloutChipIn> toolhead; };`
  - `struct CalloutLayout { CalloutMode mode; CalloutRect image; std::vector<CalloutChipOut> chips; bool toolhead_merged; };`
  - `CalloutRect fit_image(int area_w, int area_h, int img_w, int img_h)`
  - `void spread_1d(std::vector<int>& start, const std::vector<int>& size, int lo, int hi, int gap)`
  - `CalloutLayout compute_callout_layout(const CalloutLayoutInput& in)`

- [ ] **Step 1: Write the failing tests**

```cpp
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

TEST_CASE("spread_1d: resolves overlaps and stays within bounds", "[printer_image][callout_layout]") {
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

TEST_CASE("pinned: a point at the image edge is clamped inside", "[printer_image][callout_layout]") {
    auto in = base();
    in.active = {{CalloutKind::Light, 40, NormPoint{0.99f, 0.01f}}};
    const auto l = compute_callout_layout(in);
    CHECK(inside(find(l, CalloutKind::Light)->rect, in.area_w, in.area_h));
}

TEST_CASE("pinned: nozzle and fan merge into the toolhead chip", "[printer_image][callout_layout]") {
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
            const bool overlap = a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[callout_layout]'` → compile error, header missing.

- [ ] **Step 3: Implement `src/ui/panel_widgets/callout_layout.h`**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure layout decision for the printer image widget's live callouts: which
// mode the widget draws in, where the image sits, and where every chip and
// leader line goes. No LVGL, so every boundary is unit-testable. The widget
// measures chip widths in the fonts the chips render and feeds them here.

#include "printer_image_regions.h"

#include <algorithm>
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
    int w = 0; ///< measured width including padding and comfort margin
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
    r.x = std::clamp(r.x, 0, std::max(0, area_w - r.w));
    r.y = std::clamp(r.y, 0, std::max(0, area_h - r.h));
    return r;
}

/// Chips with no usable point: a column in the widest side band if one fits
/// them, else a bottom-edge row filled right to left, wrapping upward.
inline void place_docked(const CalloutLayoutInput& in, const CalloutRect& img,
                         const std::vector<CalloutChipIn>& chips, std::vector<CalloutChipOut>& out) {
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
                {c.kind,
                 clamp_into({cx - c.w / 2, cy - in.chip_h / 2, c.w, in.chip_h}, in.area_w, in.area_h)});
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
```

Passing `CalloutRect{0, 0, in.area_w, 0}` as the image to `place_docked` leaves no side band,
which forces the bottom row for unanchored chips on a tagged image.

- [ ] **Step 4: Run to verify it passes**

Run: `make t F='[callout_layout]'` → PASS (12 test cases). If a boundary test fails by one
pixel, fix the arithmetic, not the test: the tests pin the spec's rules.

- [ ] **Step 5: Mutate and commit**

Run: `make mutate-diff`. Expected: changing `3 * in.chip_h` to `2 * in.chip_h`, or removing the
merge block, fails `[callout_layout]`.

```bash
git add src/ui/panel_widgets/callout_layout.h tests/unit/test_callout_layout.cpp
git commit -m "feat(printer-image): pure callout layout: image-only, pinned with toolhead merge, docked (prestonbrown/helixscreen#1397)

Mutation: 3*chip_h to 2*chip_h fails the 2x1-cell case." -- src/ui/panel_widgets/callout_layout.h tests/unit/test_callout_layout.cpp
```

---

### Task 4: One text-measure helper, the chip component, and the callout layer

**Files:**
- Create: `src/ui/panel_widgets/text_measure.h`, `ui_xml/components/activity_chip.xml`
- Modify: `src/ui/panel_widgets/nozzle_temps_widget.cpp`, `src/ui/panel_widgets/fan_stack_widget.cpp` (drop their private `measure_text_px`), `src/xml_registration.cpp`, `ui_xml/components/panel_widget_printer_image.xml`, `src/ui/panel_widgets/printer_image_widget.cpp` (subjects only)
- Test: `tests/unit/test_printer_image_callouts.cpp` (first cases)

**Interfaces:**
- Produces:
  - `int helix::ui::measure_text_px(const char* txt, const lv_font_t* font)` in `text_measure.h`
  - XML component `activity_chip` with props `text_subject` (string subject name) and a default child slot for the icon
  - Subjects (all registered in `printer_image_widget_init_subjects()`):
    - int `printer_callout_mode` (a `CalloutMode`), int `callout_toolhead_merged`
    - int `callout_nozzle_shown`, `callout_bed_shown`, `callout_chamber_shown`, `callout_fan_shown`, `callout_light_shown`
    - string `callout_nozzle_text`, `callout_bed_text`, `callout_chamber_text`, `callout_fan_text`, `callout_toolhead_text`
    - int `callout_bed_heating`
  - Named objects under the widget: `callout_layer`, `callout_chip_{nozzle,bed,chamber,fan,light,toolhead}`, `callout_line_{nozzle,bed,chamber,fan,light}`, `callout_bed_glow`

- [ ] **Step 1: Extract the helper**

`src/ui/panel_widgets/text_measure.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

namespace helix::ui {

/// Pixel width of a UTF-8 string in `font`. lv_text_get_width dereferences its
/// attributes argument, so it gets a zeroed block (no recolor, zero letter
/// space, unbounded width); NULL crashes.
inline int measure_text_px(const char* txt, const lv_font_t* font) {
    if (!txt || !font)
        return 0;
    lv_text_attributes_t attrs;
    lv_text_attributes_init(&attrs);
    attrs.letter_space = 0;
    attrs.max_width = LV_COORD_MAX;
    return lv_text_get_width(txt, LV_TEXT_LEN_MAX, font, &attrs);
}

} // namespace helix::ui
```

Delete the anonymous-namespace `measure_text_px` from `nozzle_temps_widget.cpp` and
`fan_stack_widget.cpp`; include `text_measure.h` and add `using helix::ui::measure_text_px;`.
Run `make t F='[nozzle]'` and `make t F='[fan_stack]'` (check the tags in their test files):
both PASS unchanged.

- [ ] **Step 2: Write the failing structure test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_ui_test_fixture.h"
#include "../test_helpers/panel_widget_size_harness.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "src/ui/panel_widgets/printer_image_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE_METHOD(LVGLUITestFixture, "printer image: callout layer and chips exist, hidden when idle",
                 "[printer_image][callouts]") {
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
```

Check `PanelWidgetHarness`'s constructor in `tests/test_helpers/panel_widget_size_harness.h`
for the exact arguments a default-constructed widget takes; mirror
`tests/unit/test_widget_size_active_spool.cpp`.

Run: `make t F='[callouts]'` → FAIL, `callout_layer` not found.

- [ ] **Step 3: Write `ui_xml/components/activity_chip.xml`**

```xml
<?xml version="1.0"?>
<!-- Copyright (C) 2025-2026 356C LLC -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- A small pill: an icon (the child passed in) beside one line of bound text.
     Sized to content; the owner positions it. -->
<component>
  <api>
    <prop name="text_subject" type="subject" default=""/>
  </api>
  <view extends="lv_obj"
        width="content" height="content" flex_flow="row" style_flex_cross_place="center"
        style_pad_hor="#space_xs" style_pad_ver="#space_xxs" style_pad_column="#space_xxs"
        style_radius="#space_md" style_bg_color="#card_bg" style_bg_opa="220"
        style_border_width="1" style_border_color="#border" scrollable="false" clickable="true">
    <text_small name="chip_text" bind_text="$text_subject" long_mode="clip"/>
  </view>
</component>
```

Before using `type="subject"` and child slots, read how `ui_xml/components/heater_icon.xml`
declares props and how `panel_widget_bed_temperature.xml` passes `rung_subject="$..."`; use
the same prop type spelling. If helix-xml components cannot accept a child slot, give the chip
an `icon_src` prop and render `<icon src="$icon_src" size="xs"/>` inside instead, with the
heater chips wrapping it as `heater_icon`/`nozzle_icon` in the printer image XML (Step 4).

Register it in `src/xml_registration.cpp` next to `register_xml("status_pill.xml");`:
`register_xml("components/activity_chip.xml");` (match the path form the neighbours use).

- [ ] **Step 4: Add the callout layer to `panel_widget_printer_image.xml`**

Inside `printer_container`, after the `printer_image` element and before the overlay icons:

```xml
      <lv_obj name="callout_layer" width="100%" height="100%" style_pad_all="0"
              style_bg_opa="0" style_border_width="0" scrollable="false" clickable="false"
              ignore_layout="true">
        <bind_flag_if_eq subject="printer_callout_mode" flag="hidden" ref_value="0"/>

        <activity_chip name="callout_chip_nozzle" text_subject="callout_nozzle_text">
          <bind_flag_if cond="callout_nozzle_shown eq 0 or callout_toolhead_merged eq 1" flag="hidden"/>
          <event_cb trigger="clicked" callback="printer_callout_nozzle_cb"/>
          <nozzle_icon size="xs"/>
        </activity_chip>
        <activity_chip name="callout_chip_bed" text_subject="callout_bed_text">
          <bind_flag_if_eq subject="callout_bed_shown" flag="hidden" ref_value="0"/>
          <event_cb trigger="clicked" callback="printer_callout_bed_cb"/>
          <heater_icon src="radiator" glyph_name="bed_icon_glyph" size="xs"/>
        </activity_chip>
        <activity_chip name="callout_chip_chamber" text_subject="callout_chamber_text">
          <bind_flag_if_eq subject="callout_chamber_shown" flag="hidden" ref_value="0"/>
          <event_cb trigger="clicked" callback="printer_callout_chamber_cb"/>
          <heater_icon src="CHAMBER_GLYPH" glyph_name="chamber_icon_glyph" size="xs"/>
        </activity_chip>
        <activity_chip name="callout_chip_fan" text_subject="callout_fan_text">
          <bind_flag_if cond="callout_fan_shown eq 0 or callout_toolhead_merged eq 1" flag="hidden"/>
          <event_cb trigger="clicked" callback="printer_callout_fan_cb"/>
          <icon name="callout_fan_icon" src="fan" size="xs"/>
        </activity_chip>
        <activity_chip name="callout_chip_light">
          <bind_flag_if_eq subject="callout_light_shown" flag="hidden" ref_value="0"/>
          <event_cb trigger="clicked" callback="printer_callout_light_cb"/>
          <icon src="lightbulb_on" size="xs" variant="warning"/>
        </activity_chip>
        <activity_chip name="callout_chip_toolhead" text_subject="callout_toolhead_text">
          <bind_flag_if_eq subject="callout_toolhead_merged" flag="hidden" ref_value="0"/>
          <event_cb trigger="clicked" callback="printer_callout_nozzle_cb"/>
          <nozzle_icon size="xs"/>
          <icon name="callout_toolhead_fan_icon" src="fan" size="xs"/>
        </activity_chip>
      </lv_obj>
```

Replace `CHAMBER_GLYPH` with the `src` the chamber temperature widget uses
(`grep -n chamber_icon_glyph ui_xml/components/*.xml`). Use the exact attribute spellings the
repo uses for `ignore_layout`, `bind_flag_if cond=` and `size=` (see
`docs/devel/LVGL9_XML_GUIDE.md`); the names above are the contract, the attribute spellings are
not. Lines and glow come in Task 7.

- [ ] **Step 5: Register the subjects**

In `printer_image_widget.cpp`, extend the static subjects and `printer_image_widget_init_subjects()`
/ its deinit lambda, in the file's existing style (init, `lv_xml_register_subject`,
`SubjectDebugRegistry::register_subject`, deinit in the registered lambda). String subjects use
32-byte buffers. Also register the six event callbacks in `register_printer_image_widget()` next
to `printer_manager_clicked_cb`, each forwarding to a `PrinterImageWidget` handler added in
Task 5 (declare the static callbacks in the header now; bodies may log and return until Task 5).

- [ ] **Step 6: Run to verify it passes**

Run: `make t F='[callouts]'` → PASS. Run `make t F='[printer_image]'` → the existing
`test_printer_image_widget.cpp` cases still PASS.

- [ ] **Step 7: Commit**

```bash
git add src/ui/panel_widgets/text_measure.h ui_xml/components/activity_chip.xml tests/unit/test_printer_image_callouts.cpp
git commit -m "feat(printer-image): activity chip component and the callout layer; one shared measure_text_px (prestonbrown/helixscreen#1397)" -- src/ui/panel_widgets/text_measure.h ui_xml/components/activity_chip.xml tests/unit/test_printer_image_callouts.cpp src/ui/panel_widgets/nozzle_temps_widget.cpp src/ui/panel_widgets/fan_stack_widget.cpp src/xml_registration.cpp ui_xml/components/panel_widget_printer_image.xml src/ui/panel_widgets/printer_image_widget.cpp src/ui/panel_widgets/printer_image_widget.h
```

---

### Task 5: Widget wiring (data, measure, apply, taps)

**Files:**
- Modify: `src/ui/panel_widgets/printer_image_widget.{h,cpp}`
- Test: `tests/unit/test_printer_image_callouts.cpp`

**Interfaces:**
- Consumes: `is_residual_hot` (T1), `lookup_image_regions`, `printer_image_basename` (T2), `compute_callout_layout` and types (T3), subjects and named objects (T4), `helix::ui::temperature::heater_display`, `HeaterIconBinder`, `fan_spin_start/stop`, `observe_int_sync`.
- Produces: `void PrinterImageWidget::on_size_changed(int colspan, int rowspan, int width_px, int height_px) override`; handlers `handle_callout_clicked(CalloutKind)`.

- [ ] **Step 1: Write the failing behaviour tests**

Append to `tests/unit/test_printer_image_callouts.cpp`. Drive the mock subjects through
`h.state()` (the harness's `PrinterState&`), e.g.
`lv_subject_set_int(h.state().get_bed_target_subject(), 600)`.

Deferred timers: move `fire_one_async_call()` / `process_async_calls()` out of
`tests/unit/test_printer_image_widget.cpp` into `tests/test_helpers/lvgl_timer_pump.h` (inline,
`namespace helix::test`), include it from both files. With no printer type the widget shows
`generic-corexy`, so the tests tag that image:

```cpp
#include "../test_helpers/lvgl_timer_pump.h"
#include "printer_image_regions.h"
#include "ui_temperature_utils.h"

using helix::test::process_async_calls;

namespace {
/// Tags the fallback image with the K1C's points, so the widget under test is "tagged".
void tag_default_image() {
    ImageRegions r;
    r.src_w = 1601;
    r.src_h = 1204;
    r.nozzle = {0.513f, 0.279f};
    r.part_fan = NormPoint{0.488f, 0.206f};
    r.chamber = NormPoint{0.313f, 0.379f};
    r.light = NormPoint{0.321f, 0.164f};
    r.bed_left = {0.308f, 0.571f};
    r.bed_right = {0.611f, 0.573f};
    set_image_regions_for_testing({{"generic-corexy", r}});
}

std::string text_of(PanelWidgetHarness<PrinterImageWidget>& h, const char* chip) {
    return lv_label_get_text(lv_obj_find_by_name(h.child(chip), "chip_text"));
}
bool shown(PanelWidgetHarness<PrinterImageWidget>& h, const char* chip) {
    return !lv_obj_has_flag(h.child(chip), LV_OBJ_FLAG_HIDDEN);
}
} // namespace

TEST_CASE_METHOD(LVGLUITestFixture, "callouts: bed heating shows the bed chip with heater_display text",
                 "[printer_image][callouts]") {
    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    h.resize(4, 4, 160, 160);
    lv_subject_set_int(h.state().get_bed_temp_subject(), 580);
    lv_subject_set_int(h.state().get_bed_target_subject(), 600);
    process_async_calls();
    CHECK(shown(h, "callout_chip_bed"));
    CHECK(text_of(h, "callout_chip_bed") ==
          helix::ui::temperature::heater_display(580, 600).temp);
}

TEST_CASE_METHOD(LVGLUITestFixture, "callouts: idle cold printer shows no chips",
                 "[printer_image][callouts]") {
    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    h.resize(4, 4, 160, 160);
    lv_subject_set_int(h.state().get_bed_temp_subject(), 250);
    lv_subject_set_int(h.state().get_bed_target_subject(), 0);
    process_async_calls();
    CHECK_FALSE(shown(h, "callout_chip_bed"));
    CHECK_FALSE(shown(h, "callout_chip_nozzle"));
}

TEST_CASE_METHOD(LVGLUITestFixture, "callouts: heater off but hot keeps the chip until 50C",
                 "[printer_image][callouts]") {
    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    h.resize(4, 4, 160, 160);
    lv_subject_set_int(h.state().get_bed_target_subject(), 0);
    lv_subject_set_int(h.state().get_bed_temp_subject(), 640);
    process_async_calls();
    CHECK(shown(h, "callout_chip_bed"));
    CHECK(text_of(h, "callout_chip_bed") == helix::ui::temperature::heater_display(640, 0).temp);
    lv_subject_set_int(h.state().get_bed_temp_subject(), 500);
    process_async_calls();
    CHECK_FALSE(shown(h, "callout_chip_bed"));
}

TEST_CASE_METHOD(LVGLUITestFixture, "callouts: fan on shows percent; light needs the LED capability",
                 "[printer_image][callouts]") {
    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    h.resize(4, 4, 160, 160);
    lv_subject_set_int(h.state().get_fan_speed_subject(), 80);
    lv_subject_set_int(h.state().get_led_state_subject(), 1);
    lv_subject_set_int(h.state().get_printer_has_led_subject(), 0);
    process_async_calls();
    CHECK(shown(h, "callout_chip_fan"));
    CHECK(text_of(h, "callout_chip_fan") == "80%");
    CHECK_FALSE(shown(h, "callout_chip_light"));
    lv_subject_set_int(h.state().get_printer_has_led_subject(), 1);
    process_async_calls();
    CHECK(shown(h, "callout_chip_light"));
}

TEST_CASE_METHOD(LVGLUITestFixture, "callouts: single cell hides the whole layer",
                 "[printer_image][callouts]") {
    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    h.resize(2, 2, 80, 80);
    lv_subject_set_int(h.state().get_bed_target_subject(), 600);
    process_async_calls();
    CHECK(lv_obj_has_flag(h.child("callout_layer"), LV_OBJ_FLAG_HIDDEN));
}

// The populate_widgets reuse path: the old component is deleted under the widget,
// then the SAME instance is attached to a fresh one (#1109 shape).
TEST_CASE_METHOD(LVGLUITestFixture, "callouts: a recycled instance drives its new tree",
                 "[printer_image][callouts]") {
    tag_default_image();
    PrinterImageWidget widget;
    auto* comp1 = static_cast<lv_obj_t*>(
        lv_xml_create(test_screen(), "panel_widget_printer_image", nullptr));
    REQUIRE(comp1);
    widget.attach(comp1, test_screen());
    widget.on_size_changed(4, 4, 160, 160);
    lv_subject_set_int(get_printer_state().get_bed_target_subject(), 600);
    process_async_calls();

    lv_obj_delete(comp1);
    auto* comp2 = static_cast<lv_obj_t*>(
        lv_xml_create(test_screen(), "panel_widget_printer_image", nullptr));
    REQUIRE(comp2);
    widget.attach(comp2, test_screen());
    widget.on_size_changed(4, 4, 160, 160);
    lv_subject_set_int(get_printer_state().get_bed_target_subject(), 650);
    process_async_calls();
    CHECK_FALSE(lv_obj_has_flag(lv_obj_find_by_name(comp2, "callout_chip_bed"), LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_get_x(lv_obj_find_by_name(comp2, "callout_chip_bed")) > 0);
    widget.detach();
    lv_obj_delete(comp2);
    set_image_regions_for_testing({});
}
```

Call `tag_default_image()` at the top of every widget case above too, and
`set_image_regions_for_testing({})` at the end.

`lv_obj_delete(comp1)` runs `on_hooked_root_deleted()` while the binders still point into the
freed tree. Before writing Step 3, read `HeaterIconBinder` / `HeatingIconAnimator` for
`LV_EVENT_DELETE` handling: if they do not drop their icon on delete, `on_hooked_root_deleted()`
must release the binders WITHOUT touching LVGL objects (add a no-touch release to
`HeaterIconBinder` if needed), and `fan_spin_stop()` must not be called on freed icons. Run this
case under ASAN on zeus (`scripts/zeus-run.sh asan '[callouts]'`) before Task 5's commit.

Run: `make t F='[callouts]'` → FAIL (chips never shown).

- [ ] **Step 2: Header additions**

```cpp
#include "callout_layout.h"
#include "ui_heater_icon_binder.h"

// public:
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    static void printer_callout_nozzle_cb(lv_event_t* e);
    static void printer_callout_bed_cb(lv_event_t* e);
    static void printer_callout_chamber_cb(lv_event_t* e);
    static void printer_callout_fan_cb(lv_event_t* e);
    static void printer_callout_light_cb(lv_event_t* e);

// private:
    void arm_callout_observers();
    void update_callouts();          ///< subjects -> chip text/shown; schedules a relayout on change
    void schedule_callout_layout();  ///< one-shot deferred apply, like schedule_cache_check()
    void apply_callout_layout();     ///< measure, decide, position
    void handle_callout_clicked(CalloutKind kind);

    lv_timer_t* callout_timer_ = nullptr;
    int granted_colspan_ = 0;
    int granted_rowspan_ = 0;
    std::vector<ObserverGuard> callout_observers_;
    SubjectLifetime bed_temp_lt_, bed_target_lt_, chamber_temp_lt_, chamber_target_lt_, chamber_mode_lt_;
    HeaterIconBinder nozzle_binder_, bed_binder_, chamber_binder_, toolhead_binder_;
```

- [ ] **Step 3: Implement data flow**

`attach()`: after `reload_from_config()`, call `arm_callout_observers()` then
`schedule_callout_layout()`. `detach()`: clear `callout_observers_`, `unbind()` the four binders,
`fan_spin_stop()` on both fan icons if the tree is alive, and delete `callout_timer_` alongside
the other timers. `on_hooked_root_deleted()`: null `callout_timer_` handling mirrors the other
timers (the timer callback re-checks `widget_obj_`).

```cpp
void PrinterImageWidget::arm_callout_observers() {
    callout_observers_.clear();
    auto& ps = get_printer_state();
    const auto on_change = [](PrinterImageWidget* w, int) { w->update_callouts(); };
    const auto& life = ps.get_subjects_lifetime();
    for (lv_subject_t* s : {ps.get_active_extruder_temp_subject(), ps.get_active_extruder_target_subject(),
                            ps.get_fan_speed_subject(), ps.get_led_state_subject(),
                            ps.get_printer_has_led_subject(), ps.get_printer_has_chamber_heater_subject()})
        callout_observers_.push_back(helix::ui::observe_int_sync<PrinterImageWidget>(s, this, on_change, life));
    callout_observers_.push_back(helix::ui::observe_int_sync<PrinterImageWidget>(
        ps.get_bed_temp_subject(bed_temp_lt_), this, on_change, bed_temp_lt_));
    callout_observers_.push_back(helix::ui::observe_int_sync<PrinterImageWidget>(
        ps.get_bed_target_subject(bed_target_lt_), this, on_change, bed_target_lt_));
    callout_observers_.push_back(helix::ui::observe_int_sync<PrinterImageWidget>(
        ps.get_chamber_temp_subject(chamber_temp_lt_), this, on_change, chamber_temp_lt_));
    callout_observers_.push_back(helix::ui::observe_int_sync<PrinterImageWidget>(
        ps.get_chamber_effective_target_subject(chamber_target_lt_), this, on_change, chamber_target_lt_));

    nozzle_binder_.bind(lv_obj_find_by_name(widget_obj_, "callout_chip_nozzle"), ps, HeaterType::Nozzle);
    toolhead_binder_.bind(lv_obj_find_by_name(widget_obj_, "callout_chip_toolhead"), ps, HeaterType::Nozzle);
    bed_binder_.bind(lv_obj_find_by_name(widget_obj_, "callout_chip_bed"), ps, HeaterType::Bed);
    chamber_binder_.bind(lv_obj_find_by_name(widget_obj_, "callout_chip_chamber"), ps, HeaterType::Chamber);
    update_callouts();
}
```

Check `observe_int_sync`'s exact namespace and handler signature in `include/observer_factory.h`,
and the lifetime-overload names in `include/printer_state.h`; the capability getters live on
`PrinterCapabilitiesState` (`get_printer_has_led_subject()`), reach them the way other widgets do
(`grep -rn get_printer_has_led_subject src/ui`).

```cpp
void PrinterImageWidget::update_callouts() {
    using namespace helix::ui::temperature;
    auto& ps = get_printer_state();
    const auto get = [](lv_subject_t* s) { return s ? lv_subject_get_int(s) : 0; };
    bool changed = false;
    const auto publish = [&](lv_subject_t* shown, int v, lv_subject_t* text, const std::string& t) {
        if (lv_subject_get_int(shown) != v) {
            lv_subject_set_int(shown, v);
            changed = true;
        }
        if (text && t != lv_subject_get_string(text)) {
            lv_subject_copy_string(text, t.c_str());
            changed = true;
        }
    };
    const auto heater = [&](int cur, int tgt, bool capable, lv_subject_t* shown, lv_subject_t* text) {
        const bool on = capable && (tgt > 0 || is_residual_hot(cur));
        publish(shown, on ? 1 : 0, text, on ? heater_display(cur, tgt).temp : std::string());
        return on;
    };

    const int noz_cur = get(ps.get_active_extruder_temp_subject());
    const int noz_tgt = get(ps.get_active_extruder_target_subject());
    heater(noz_cur, noz_tgt, true, &s_callout_nozzle_shown, &s_callout_nozzle_text);
    const int bed_cur = get(ps.get_bed_temp_subject(bed_temp_lt_));
    const int bed_tgt = get(ps.get_bed_target_subject(bed_target_lt_));
    heater(bed_cur, bed_tgt, true, &s_callout_bed_shown, &s_callout_bed_text);
    lv_subject_set_int(&s_callout_bed_heating,
                       classify_heat_state(displayed_deci(bed_cur), bed_tgt, DEFAULT_AT_TEMP_TOLERANCE_DECI) ==
                               HeatState::Heating ? 1 : 0);
    heater(get(ps.get_chamber_temp_subject(chamber_temp_lt_)),
           get(ps.get_chamber_effective_target_subject(chamber_target_lt_)),
           get(ps.get_printer_has_chamber_heater_subject()) != 0, &s_callout_chamber_shown,
           &s_callout_chamber_text);

    const int fan = get(ps.get_fan_speed_subject());
    char fan_buf[8];
    snprintf(fan_buf, sizeof(fan_buf), "%d%%", fan);
    publish(&s_callout_fan_shown, fan > 0 ? 1 : 0, &s_callout_fan_text, fan > 0 ? fan_buf : "");
    publish(&s_callout_light_shown,
            get(ps.get_printer_has_led_subject()) && get(ps.get_led_state_subject()) ? 1 : 0,
            nullptr, {});
    const std::string toolhead = std::string(lv_subject_get_string(&s_callout_nozzle_text)) + "  " + fan_buf;
    if (toolhead != lv_subject_get_string(&s_callout_toolhead_text))
        lv_subject_copy_string(&s_callout_toolhead_text, toolhead.c_str());

    if (widget_obj_) {
        for (const char* n : {"callout_fan_icon", "callout_toolhead_fan_icon"}) {
            if (lv_obj_t* icon = lv_obj_find_by_name(widget_obj_, n))
                fan > 0 ? fan_spin_start(icon, fan) : fan_spin_stop(icon);
        }
    }
    if (changed)
        schedule_callout_layout();
}
```

The fan spin must honour the animations preference the way
`fan_stack_widget.cpp#update_fan_animation` does; copy that check, do not skip it.

- [ ] **Step 4: Implement measure, decide, apply**

```cpp
void PrinterImageWidget::on_size_changed(int colspan, int rowspan, int, int) {
    granted_colspan_ = colspan;
    granted_rowspan_ = rowspan;
    schedule_callout_layout();
}

void PrinterImageWidget::schedule_callout_layout() {
    if (callout_timer_) {
        lv_timer_delete(callout_timer_);
        callout_timer_ = nullptr;
    }
    // Deferred like the cache check: sizes are read only after the grid has laid
    // the widget out, and nothing here may force layout during a rebuild (#983).
    callout_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            LVGL_SAFE_EVENT_CB_BEGIN("[PrinterImageWidget] callout_timer");
            auto* self = static_cast<PrinterImageWidget*>(lv_timer_get_user_data(timer));
            if (self) {
                self->callout_timer_ = nullptr;
                self->apply_callout_layout();
            }
            lv_timer_delete(timer);
            LVGL_SAFE_EVENT_CB_END();
        },
        50, this);
    lv_timer_set_repeat_count(callout_timer_, 1);
}
```

`apply_callout_layout()`:

1. Bail if `!widget_obj_`. Find `printer_container`; read `lv_obj_get_content_width/height`. A
   zero size publishes `CalloutMode::ImageOnly` and returns (the next size change reschedules).
2. Regions: `const ImageRegions* r = lookup_image_regions(printer_image_basename(current_source_path_));`.
3. Image aspect: `r->src_w/src_h` when tagged. Otherwise
   `lv_image_header_t hdr; lv_image_decoder_get_info(current_source_path_.c_str(), &hdr)`
   (precedent `src/helix_splash.cpp`); on failure publish ImageOnly and return.
4. Measure, with `measure_text_px` and fonts from `theme_manager_get_font("font_small")` /
   `("icon_font_xs")` (the fonts `text_small` and `size="xs"` icons render in; confirm against
   `nozzle_temps_widget.cpp#on_size_changed`):
   `chip_w(text) = icon_px + space_xxs + measure_text_px(text) + 2*space_xs + comfort(space_md)`;
   `chip_h = font line height + 2*space_xxs`. Measure active chips from the subject text they show,
   and the budget from the widest strings the same composers produce:
   `heater_display(9990, 9990).temp` for heaters, `"100%"` for the fan, icon-only for the light.
5. Anchors from `r`: nozzle → `nozzle`, fan → `part_fan`, bed → midpoint of `bed_left`/`bed_right`,
   chamber → `chamber`, light → `light`, toolhead → `nozzle`.
6. Budget set: nozzle, bed, fan always; chamber if `printer_has_chamber_heater`; light if
   `printer_has_led`. Active set: kinds whose `callout_*_shown` is 1.
7. `single_cell = granted_colspan_ <= GridLayout::TRACKS_PER_CELL && granted_rowspan_ <= GridLayout::TRACKS_PER_CELL`
   (`#include "grid_layout.h"`).
8. Call `compute_callout_layout()`. Publish `printer_callout_mode` and `callout_toolhead_merged`.
   For each output chip: `lv_obj_set_pos(chip, rect.x, rect.y); // DECLARATIVE_OK: measured callout layout`.
   Chip objects are children of `callout_layer`, which fills `printer_container`, so rect
   coordinates are layer-local.

- [ ] **Step 5: Taps**

Each static callback recovers the widget from the chip's parent chain (`callout_layer` →
`printer_container`, whose user_data `attach()` already sets) and calls
`handle_callout_clicked(kind)`. Stop propagation so the container's Printer Manager click does
not also fire: set the chip `clickable` (Task 4 XML) and add `event_bubble="false"` if the
container still receives it (verify with the harness: a chip click must not push the Printer
Manager overlay).

```cpp
void PrinterImageWidget::handle_callout_clicked(CalloutKind kind) {
    switch (kind) {
    case CalloutKind::Nozzle:
    case CalloutKind::Toolhead:
        get_global_temp_graph_overlay().open(TempGraphOverlay::Mode::Nozzle, parent_screen_);
        break;
    case CalloutKind::Bed:
        get_global_temp_graph_overlay().open(TempGraphOverlay::Mode::Bed, parent_screen_);
        break;
    case CalloutKind::Chamber:
        get_global_temp_graph_overlay().open(TempGraphOverlay::Mode::Chamber, parent_screen_);
        break;
    case CalloutKind::Fan:
        open_fan_control_overlay();   // body copied from FanWidget::handle_clicked
        break;
    case CalloutKind::Light:
        open_led_control_overlay();   // body copied from LedControlsWidget::handle_clicked
        break;
    }
}
```

Copy-paste of the fan and LED open sequences would make a third and second copy. Instead,
extract each into a free function beside its overlay (`open_fan_control_overlay(lv_obj_t* parent)`
in the fan control overlay's header/source, `open_led_control_overlay(lv_obj_t* parent)` likewise),
switch `FanWidget::handle_clicked` / `LedControlsWidget::handle_clicked` to call them, and call
them here. Run those widgets' tests after the extraction.

Add a tap-routing test: click `callout_chip_bed` via `lv_obj_send_event(chip, LV_EVENT_CLICKED, nullptr)`
and assert the temp graph overlay is open in `Mode::Bed` (see how temp graph tests assert the
mode), and that the Printer Manager overlay is not on the nav stack.

- [ ] **Step 6: Run to verify it passes**

Run: `make t F='[callouts]'` then `make t F='[printer_image]'` → PASS.

- [ ] **Step 7: Mutate and commit**

Run: `make mutate-diff`. Expected: removing `is_residual_hot` from the heater predicate, or the
`printer_has_led` gate, or the `arm_callout_observers()` call in `attach()`, fails `[callouts]`.

```bash
git commit -m "feat(printer-image): live callout chips for nozzle, bed, chamber, fan and light (prestonbrown/helixscreen#1397)

Mutation: dropping is_residual_hot from the heater predicate fails [callouts]." -- src/ui/panel_widgets/printer_image_widget.h src/ui/panel_widgets/printer_image_widget.cpp tests/unit/test_printer_image_callouts.cpp <the fan/LED overlay files the extraction touched>
```

---

### Task 6: Pure layout, phase 2 modes (both sides, one side)

**Files:**
- Modify: `src/ui/panel_widgets/callout_layout.h`
- Test: `tests/unit/test_callout_layout.cpp`

**Interfaces:**
- Produces: `compute_callout_layout()` returns `BothSides` / `OneSide` with `has_line` chips and a
  moved `image` rect for `OneSide`.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
// K1C in a 4x2 widget at 800x480: 320x140 container, fitted image 186 wide.
CalloutLayoutInput wide() {
    auto in = base();
    in.area_w = 320;
    in.area_h = 140;
    in.budget = {{CalloutKind::Nozzle, 70, NormPoint{0.51f, 0.28f}},
                 {CalloutKind::Bed, 70, NormPoint{0.46f, 0.57f}},
                 {CalloutKind::Fan, 45, NormPoint{0.49f, 0.21f}},
                 {CalloutKind::Chamber, 70, NormPoint{0.31f, 0.38f}}};
    in.active = in.budget;
    return in;
}
} // namespace

TEST_CASE("both sides when each band fits a column", "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line); // exactly one column per side
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::BothSides);
    CHECK(l.image.x == (in.area_w - l.image.w) / 2); // image stays centred
    for (const auto& c : l.chips) {
        CHECK(c.has_line);
        const bool left = c.rect.x + c.rect.w <= l.image.x;
        const bool right = c.rect.x >= l.image.x + l.image.w;
        CHECK((left || right));
    }
    // Chamber's point is left of centre, so its chip is on the left.
    for (const auto& c : l.chips)
        if (c.kind == CalloutKind::Chamber)
            CHECK(c.rect.x < l.image.x);
}

TEST_CASE("one pixel short of both sides falls to one side", "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line) - 2;
    const auto l = compute_callout_layout(in);
    REQUIRE(l.mode == CalloutMode::OneSide);
    CHECK(l.image.x == 0); // image moved left
    for (const auto& c : l.chips) {
        CHECK(c.has_line);
        CHECK(c.rect.x >= l.image.w);
    }
}

TEST_CASE("column that cannot stack its chips falls through to pinned",
          "[printer_image][callout_layout]") {
    auto in = wide();
    for (auto& c : in.budget)
        c.anchor->x = 0.3f; // every point left of centre: one side must hold all four
    in.active = in.budget;
    in.area_w = 400;
    in.area_h = 4 * in.chip_h + 3 * in.gap + 2 * in.gap - 1; // one pixel short for four stacked
    CHECK(compute_callout_layout(in).mode == CalloutMode::Pinned);
    in.area_h += 1;
    CHECK(compute_callout_layout(in).mode != CalloutMode::Pinned);
}

TEST_CASE("mode comes from the budget, so the image does not move when chips change",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line) - 2;
    const auto full = compute_callout_layout(in);
    in.active = {{CalloutKind::Bed, 70, NormPoint{0.46f, 0.57f}}};
    const auto one = compute_callout_layout(in);
    CHECK(one.mode == full.mode);
    CHECK(one.image.x == full.image.x);
    in.active.clear();
    CHECK(compute_callout_layout(in).image.x == full.image.x);
}

TEST_CASE("leader line runs from the tagged point to the chip's inner edge",
          "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 186 + 2 * (70 + in.gap + in.min_line);
    in.active = {{CalloutKind::Bed, 70, NormPoint{0.46f, 0.57f}}};
    const auto l = compute_callout_layout(in);
    REQUIRE(l.chips.size() == 1);
    const auto& c = l.chips[0];
    CHECK(c.line_x0 == l.image.x + int(0.46f * l.image.w));
    CHECK(c.line_y0 == l.image.y + int(0.57f * l.image.h));
    const bool at_inner_edge = c.line_x1 == c.rect.x || c.line_x1 == c.rect.x + c.rect.w;
    CHECK(at_inner_edge);
    CHECK(c.line_y1 == c.rect.y + c.rect.h / 2);
}

TEST_CASE("tall widget uses bands above and below", "[printer_image][callout_layout]") {
    auto in = wide();
    in.area_w = 240;
    in.area_h = 480; // image 240x180 centred: 150px above and below
    const auto l = compute_callout_layout(in);
    REQUIRE((l.mode == CalloutMode::BothSides || l.mode == CalloutMode::OneSide));
    for (const auto& c : l.chips) {
        const bool above = c.rect.y + c.rect.h <= l.image.y;
        const bool below = c.rect.y >= l.image.y + l.image.h;
        CHECK((above || below));
    }
}
```

Run: `make t F='[callout_layout]'` → the six new cases FAIL (mode is Pinned).

- [ ] **Step 2: Implement**

Add to `callout_detail`:

```cpp
inline CalloutChipOut chip_at(const CalloutChipIn& c, int x, int y, int h, int ax, int ay) {
    CalloutChipOut o{c.kind, {x, y, c.w, h}, true};
    o.line_x0 = ax;
    o.line_y0 = ay;
    return o;
}

/// Stack `chips` (sorted by the point's position along the stacking axis) in a
/// column at x = col_x (horizontal bands) or a row at y = row_y (vertical bands).
/// Returns false if they do not fit the axis.
inline bool stack(const CalloutLayoutInput& in, const CalloutRect& img, bool horizontal_band,
                  bool left_or_top, int band_pos, std::vector<CalloutChipIn> chips,
                  std::vector<CalloutChipOut>& out) {
    if (chips.empty())
        return true;
    const auto key = [&](const CalloutChipIn& c) { return horizontal_band ? c.anchor->y : c.anchor->x; };
    std::sort(chips.begin(), chips.end(), [&](auto& a, auto& b) { return key(a) < key(b); });
    std::vector<int> start, size;
    for (const auto& c : chips) {
        const int a = horizontal_band ? px(c.anchor->y, img.y, img.h) : px(c.anchor->x, img.x, img.w);
        const int s = horizontal_band ? in.chip_h : c.w;
        start.push_back(a - s / 2);
        size.push_back(s);
    }
    const int extent = horizontal_band ? in.area_h : in.area_w;
    int total = in.gap * int(chips.size() - 1);
    for (int s : size)
        total += s;
    if (total > extent - 2 * in.gap)
        return false;
    spread_1d(start, size, in.gap, extent - in.gap, in.gap);
    for (size_t i = 0; i < chips.size(); ++i) {
        const auto& c = chips[i];
        const int ax = px(c.anchor->x, img.x, img.w), ay = px(c.anchor->y, img.y, img.h);
        if (horizontal_band) {
            const int x = left_or_top ? band_pos : band_pos - c.w;
            auto o = chip_at(c, x, start[i], in.chip_h, ax, ay);
            o.line_x1 = left_or_top ? x + c.w : x;
            o.line_y1 = start[i] + in.chip_h / 2;
            out.push_back(o);
        } else {
            const int y = left_or_top ? band_pos : band_pos - in.chip_h;
            auto o = chip_at(c, start[i], y, in.chip_h, ax, ay);
            o.line_x1 = start[i] + c.w / 2;
            o.line_y1 = left_or_top ? y + in.chip_h : y;
            out.push_back(o);
        }
    }
    return true;
}
```

Then, still in `callout_detail`:

```cpp
/// Both sides, else one side. Fit is tested against the BUDGET and only the
/// ACTIVE chips are placed, so the image never moves as chips come and go.
/// A part with no tagged point cannot have a line: the pinned path docks it.
inline bool try_line_modes(const CalloutLayoutInput& in, CalloutLayout& out) {
    if (in.budget.empty())
        return false;
    for (const auto* v : {&in.budget, &in.active})
        for (const auto& c : *v)
            if (!c.anchor)
                return false;

    const CalloutRect centred = out.image;
    const bool horiz = (in.area_w - centred.w) >= (in.area_h - centred.h);
    int widest = 0;
    for (const auto& c : in.budget)
        widest = std::max(widest, c.w);
    const int col = (horiz ? widest : in.chip_h) + in.gap + in.min_line;
    const int far_edge = (horiz ? in.area_w : in.area_h) - in.gap;
    const auto split = [&](const std::vector<CalloutChipIn>& v, bool near_half) {
        std::vector<CalloutChipIn> r;
        for (const auto& c : v)
            if (((horiz ? c.anchor->x : c.anchor->y) < 0.5f) == near_half)
                r.push_back(c);
        return r;
    };

    std::vector<CalloutChipOut> scratch;
    const int band = horiz ? centred.x : centred.y;
    if (band >= col && stack(in, centred, horiz, true, in.gap, split(in.budget, true), scratch) &&
        stack(in, centred, horiz, false, far_edge, split(in.budget, false), scratch)) {
        out.chips.clear();
        stack(in, centred, horiz, true, in.gap, split(in.active, true), out.chips);
        stack(in, centred, horiz, false, far_edge, split(in.active, false), out.chips);
        out.mode = CalloutMode::BothSides;
        return true;
    }

    const int free_extent = horiz ? in.area_w - centred.w : in.area_h - centred.h;
    CalloutRect moved = centred;
    (horiz ? moved.x : moved.y) = 0;
    scratch.clear();
    if (free_extent >= col && stack(in, moved, horiz, false, far_edge, in.budget, scratch)) {
        out.image = moved;
        out.chips.clear();
        stack(in, moved, horiz, false, far_edge, in.active, out.chips);
        out.mode = CalloutMode::OneSide;
        return true;
    }
    return false;
}
```

In `compute_callout_layout`, replace the `// Phase 2 inserts the leader-line modes here, ahead of
pinned.` line with:

```cpp
        if (try_line_modes(in, out))
            return out;
```

Toolhead merging does not apply in line modes.

- [ ] **Step 3: Run, mutate, commit**

Run: `make t F='[callout_layout]'` → PASS (all cases, phase 1 included).
Run: `make mutate-diff`. Expected: swapping the both-sides and one-side order, or deciding
fit from `in.active`, fails `[callout_layout]`.

```bash
git commit -m "feat(printer-image): leader-line layouts, both sides or one, decided from the worst-case chip set (prestonbrown/helixscreen#1397)

Mutation: fitting against active instead of budget fails the image-stays-put case." -- src/ui/panel_widgets/callout_layout.h tests/unit/test_callout_layout.cpp
```

---

### Task 7: Leader lines, bed glow, and moving the image

**Files:**
- Modify: `ui_xml/components/panel_widget_printer_image.xml`, `src/ui/panel_widgets/printer_image_widget.{h,cpp}`
- Test: `tests/unit/test_printer_image_callouts.cpp`

**Interfaces:**
- Consumes: `CalloutLayout` line fields and `image` rect (T6), `callout_bed_heating` (T4/T5).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE_METHOD(LVGLUITestFixture, "callouts: wide widget draws a line to the bed chip",
                 "[printer_image][callouts]") {
    tag_default_image();
    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    h.resize(8, 4, 320, 160);
    lv_subject_set_int(h.state().get_bed_target_subject(), 600);
    lv_subject_set_int(h.state().get_bed_temp_subject(), 400);
    process_async_calls();
    CHECK_FALSE(lv_obj_has_flag(h.child("callout_line_bed"), LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(h.child("callout_bed_glow"), LV_OBJ_FLAG_HIDDEN));
    lv_subject_set_int(h.state().get_bed_temp_subject(), 600); // at target: glow off, line stays
    process_async_calls();
    CHECK(lv_obj_has_flag(h.child("callout_bed_glow"), LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(h.child("callout_line_bed"), LV_OBJ_FLAG_HIDDEN));
    set_image_regions_for_testing({});
}

TEST_CASE_METHOD(LVGLUITestFixture, "callouts: pinned mode draws no lines",
                 "[printer_image][callouts]") {
    tag_default_image();
    PanelWidgetHarness<PrinterImageWidget> h(test_screen());
    h.resize(4, 4, 160, 160);
    lv_subject_set_int(h.state().get_bed_target_subject(), 600);
    process_async_calls();
    CHECK(lv_obj_has_flag(h.child("callout_line_bed"), LV_OBJ_FLAG_HIDDEN));
    set_image_regions_for_testing({});
}
```

Run: `make t F='[callouts]'` → FAIL (`callout_line_bed` not found).

- [ ] **Step 2: XML**

Inside `callout_layer`, BEFORE the chips (so lines draw under them):

```xml
        <lv_obj name="callout_bed_glow" style_radius="LV_RADIUS_CIRCLE" style_bg_color="#danger"
                style_bg_opa="60" style_border_width="0" clickable="false">
          <bind_flag_if cond="callout_bed_heating eq 0 or printer_callout_mode eq 0" flag="hidden"/>
        </lv_obj>
        <lv_line name="callout_line_nozzle" style_line_width="1" style_line_color="#text_muted">
          <bind_flag_if cond="printer_callout_mode lt 3 or callout_nozzle_shown eq 0" flag="hidden"/>
        </lv_line>
        <!-- same for bed, chamber, fan (callout_fan_shown), light (callout_light_shown) -->
```

Write all five lines out; do not generate them.

- [ ] **Step 3: Apply lines, glow and image rect in `apply_callout_layout()`**

- For each output chip with `has_line`, set its line's points:
  `lv_point_precise_t pts[2] = {{c.line_x0, c.line_y0}, {c.line_x1, c.line_y1}};` stored in a
  member array per kind (LVGL keeps the pointer), then `lv_line_set_points(line, pts, 2); // DECLARATIVE_OK: measured callout layout`.
- Glow: an ellipse over the bed's near edge: `x = bed_left.x px`, `w = |bed_right.x - bed_left.x| px`,
  `h = w / 4`, centred on the edge's mid y. Set pos and size (DECLARATIVE_OK). Pulse its opacity
  with an `lv_anim` between 30 and 70 while shown, honouring the animations preference
  (animations are a structural exception). No shadow, no blur.
- Image rect: when the mode is `OneSide`, size and place `printer_image` to `out.image`
  (DECLARATIVE_OK); otherwise restore it to fill the container. If the rect changed, call
  `schedule_image_refresh()` so the exact-size cache regenerates for the new size. Confirm first
  whether `printer_container`'s `flex_flow="row"` fights explicit positioning; if it does, add
  `ignore_layout` to `printer_image` in XML and size it 100% there, so C++ only overrides in
  OneSide.

- [ ] **Step 4: Run, mutate, commit**

Run: `make t F='[callouts]'` then `make t F='[printer_image]'` → PASS.
Run: `make mutate-diff`. Expected: dropping the `printer_callout_mode lt 3` term from a line's
`cond` fails "pinned mode draws no lines".

```bash
git commit -m "feat(printer-image): leader lines, bed glow, and the image moves aside for a one-side column (prestonbrown/helixscreen#1397)

Mutation: removing the mode term from the line cond fails the pinned-no-lines case." -- ui_xml/components/panel_widget_printer_image.xml src/ui/panel_widgets/printer_image_widget.h src/ui/panel_widgets/printer_image_widget.cpp tests/unit/test_printer_image_callouts.cpp
```

---

### Task 8: Live verification, recipes, gates

**Files:**
- Modify: `scripts/screenshot-recipes.sh`

- [ ] **Step 1: Build and run the mock, pinned socket**

```bash
make -j"$(scripts/helix-claim jobs)"
TREE=$(basename "$(git rev-parse --show-toplevel)")
export HELIX_SOCK="/tmp/helix-$TREE.sock" HELIX_CONFIG_DIR="/tmp/helix-config-$TREE"
mkdir -p "$HELIX_CONFIG_DIR"
HELIX_MOCK_AUTO_PRINT=1 SDL_VIDEODRIVER=dummy ./build/bin/helix-screen --test --sim-speed 6 -vv \
  --remote-socket "$HELIX_SOCK" > /tmp/helix-$TREE.log 2>&1 &
echo $! > /tmp/helix-$TREE.pid
```

Pick the mock printer (see `docs/devel/MOCK_ENVIRONMENT_VARIABLES.md`) as a tagged image
(K1C or AD5X) for one run and an untagged one for another. Repeat at 480×272 and 1024×600
(size flag: `./build/bin/helix-screen --help`).

- [ ] **Step 2: Check geometry, not pixels**

For each of 1×1, 2×2, 4×2 and 4×3 cells (resize the widget via edit mode through `ctl`, or seed a
layout JSON; see `docs/devel/HELIXCTL.md`), once printing:

```bash
./build/bin/helix-screen ctl -s "$HELIX_SOCK" geom callout_chip_bed
./build/bin/helix-screen ctl -s "$HELIX_SOCK" geom printer_image
./build/bin/helix-screen ctl -s "$HELIX_SOCK" text callout_chip_bed
```

Pass: at 1×1 the layer is hidden; at 2×2 chips sit inside the image rect with no lines; at 4×2
the image's x is 0 (one side) or centred (both sides) and every chip is outside the image rect;
no chip box extends past the widget; the bed chip's text equals the bed tile's.

- [ ] **Step 3: Screenshot recipes**

Add tokens to `scripts/screenshot-recipes.sh` following its existing entries: `callouts-2x2`,
`callouts-4x2`, `callouts-untagged`. Capture each with `./scripts/screenshot.sh`. The main session
opens every PNG before reporting.

- [ ] **Step 4: Gates**

```bash
make full-test-run
python3 scripts/check_imperative_ui.py --summary   # count must not rise beyond DECLARATIVE_OK sites
python3 scripts/check_hardcoded_pixels.py
```

Kill the mock by its captured PID: `kill "$(cat /tmp/helix-$TREE.pid)"`.

- [ ] **Step 5: Commit**

```bash
git commit -m "test(printer-image): screenshot recipes for callout layouts (prestonbrown/helixscreen#1397)" -- scripts/screenshot-recipes.sh
```

# Settings Information Architecture Reorg Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize HelixScreen's settings from 6 catch-all categories into 12 single-concern pages under a grouped-list root with live status lines (prestonbrown/helixscreen#1023).

**Architecture:** XML moves rows between overlay files; `DisplaySoundSettingsOverlay` splits into four overlay classes along its existing per-row `init_*`/`handle_*` seams; two new overlays (Connection, Updates) follow the `TouchSettingsOverlay` singleton pattern. The root becomes three `setting_group` cards. Status strings come from pure formatter functions, recomputed in a new `SettingsPanel::on_activate()` override.

**Tech Stack:** C++17, LVGL 9.5 + helix-xml (`lib/helix-xml`, ours), Catch2 (`tests/unit`, auto-globbed), spdlog, Makefile build.

**Spec:** `docs/devel/plans/2026-09-27-settings-ia-reorg-design.md` (read it first; it holds the full row-by-row tree).

## Global Constraints

- Work only in `.worktrees/1023-settings-ia` (branch `feature/1023-settings-ia`). This session holds `worktree:1023-settings-ia`; a subagent building here must be the only `make` in this tree.
- No `settings.json` key changes. Every subject and settings key keeps its name.
- A row keeps its `name=` when it moves between overlays.
- Every XML event callback name ends with exactly one registrar (registration is last-write-wins, `lib/helix-xml/src/xml/lv_xml.c:1027`).
- New `ui_xml/*.xml` files are registered explicitly in `src/xml_registration.cpp` (settings block ~757-772) and classified in `tests/unit/test_job_holds_machine.cpp` `kNoMachineControlFiles` (alphabetical).
- New/removed `src/ui/*.cpp` files are mirrored in `firmware/helixscreen-esp32/components/helixapp/app_srcs.txt`.
- User-facing strings: XML `label="X" label_tag="X"`; C++ `lv_tr("X")`, formatted with `fmt::format(lv_tr("{}% brightness"), n)`. Keys are the English text.
- Code standards: SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later`, spdlog only, classes in `helix::settings`, no comment archaeology, no em-dashes in user-facing text.
- Commits: `git commit -m "..."` with a simple double-quoted message, subject `feat(settings): ...`/`refactor(settings): ...`/`docs(settings): ...` ending `(prestonbrown/helixscreen#1023)`. Body ~4 lines, one line naming the mutation that proves the test. Run `git show --stat HEAD` after every commit.
- Build/test: `make -j$(scripts/helix-claim jobs)` for the app; `make t F='[tag]'` for a tag. Never pipe a build through `head`/`tail`/`grep`.
- Mock UI checks: pinned socket per `CLAUDE.md` Quick Start (`HELIX_SOCK=/tmp/helix-1023-settings-ia.sock`, `HELIX_CONFIG_DIR=/tmp/helix-config-1023-settings-ia`), kill only the captured PID.
- Coordination: branch `fix/controls-tools-row-fit` (unmerged, 5 commits) edits `src/ui/setting_group.cpp` and `ui_xml/setting_group_header.xml`. Before Task 7, check whether it merged; if not, message its owner (`scripts/helix-claim list`) before touching either file. This plan does not need to edit them.

## Review Focus

1. **Screen rotation or resize while a settings page is showing.** A `ui_breakpoint` change after creation must not resurrect an empty description line or an (i) icon. Pinned in Task 1 (flip test).
2. **Printer with no speaker and updates managed by firmware.** Root must show Sound hidden, Updates visible with "Managed by firmware"; with updates fully suppressed, Updates hidden. Pinned in Task 7.
3. **Entry points from outside Settings.** Printer-manager speaker chip, upgrade banner, update-notify "Install", theme editor's explorer sync, and the touch-calibration row description all land on the new pages. Pinned in Tasks 3 and 4.
4. **Returning to the root after changing a value.** Change brightness/volume/language in a sub-page, go back: the root status reflects the new value. Pinned in Task 8 (refresh test) plus the manual walk in Task 11.
5. **Wi-Fi manager absent or Ethernet-only boxes.** Connection status must not crash when `get_wifi_manager()` returns null and must say "Ethernet" on a wired box. Pinned in Task 2 (formatter) and Task 8 (null manager path).

---

### Task 1: `setting_action_row` - empty description never shows

**Files:**
- Modify: `ui_xml/setting_action_row.xml` (description column ~42-56, info button ~57-65)
- Create: `tests/unit/test_setting_action_row_empty_description.cpp`
- Grep-and-fix: any C++ that finds a bound row's `"description"` label by name (`grep -rn '"description"' src/ | grep find_by_name`)

**Interfaces:**
- Produces: `setting_action_row` children `description_wrap` > `description` (static text), `status_wrap` > `status` (bound text via `bind_description`), `info_wrap` > `info_btn`. Later tasks bind root status through `bind_description` and read the `status` label in tests.

`hidden_if_empty` runs once at creation (`lib/helix-xml/src/xml/parsers/lv_xml_obj_parser.c:160`) and the `ui_breakpoint` binds rewrite the same HIDDEN flag, so today a breakpoint change clears it. Put each emptiness check on a wrapper no bind touches, and keep the breakpoint binds on the inner widget.

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "ui_update_queue.h"

#include "../catch_amalgamated.hpp"

namespace {

struct EmptyDescFixture : LVGLUITestFixture {
    lv_subject_t* bp_ = nullptr;
    int saved_bp_ = 0;
    lv_subject_t bound_{};
    char bound_buf_[32] = "0.8 mm";

    EmptyDescFixture() {
        bp_ = lv_xml_get_subject(nullptr, "ui_breakpoint");
        REQUIRE(bp_ != nullptr);
        saved_bp_ = lv_subject_get_int(bp_);
        lv_xml_register_event_cb(nullptr, "test_empty_desc_noop", [](lv_event_t*) {});
        lv_subject_init_string(&bound_, bound_buf_, nullptr, sizeof(bound_buf_), bound_buf_);
        lv_xml_register_subject(nullptr, "test_empty_desc_bound", &bound_);
    }
    ~EmptyDescFixture() override {
        lv_subject_set_int(bp_, saved_bp_);
        helix::ui::UpdateQueue::instance().drain();
    }

    lv_obj_t* make(const char* description, const char* bind, const char* min_bp) {
        const char* attrs[] = {"name", "row_under_test", "label", "Row", "icon", "cog",
                               "callback", "test_empty_desc_noop",
                               "description", description,
                               bind ? "bind_description" : nullptr, bind,
                               bind ? "description_min_bp" : nullptr, min_bp,
                               nullptr};
        auto* row = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "setting_action_row", attrs));
        REQUIRE(row != nullptr);
        process_lvgl(5);
        return row;
    }

    // Hidden if the widget or any ancestor up to `row` carries HIDDEN.
    static bool shown(lv_obj_t* row, const char* name) {
        lv_obj_t* obj = lv_obj_find_by_name(row, name);
        REQUIRE(obj != nullptr);
        for (lv_obj_t* o = obj; o && o != row; o = lv_obj_get_parent(o)) {
            if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
        }
        return true;
    }
};

}  // namespace

TEST_CASE_METHOD(EmptyDescFixture, "setting_action_row: no description shows no second line and no info icon",
                 "[xml][settings][setting_action_row]") {
    for (int bp = 0; bp <= 5; ++bp) {
        CAPTURE(bp);
        lv_subject_set_int(bp_, bp);
        lv_obj_t* row = make("", nullptr, nullptr);
        CHECK_FALSE(shown(row, "description"));
        CHECK_FALSE(shown(row, "status"));
        CHECK_FALSE(shown(row, "info_btn"));
        lv_obj_delete(row);
    }
}

TEST_CASE_METHOD(EmptyDescFixture, "setting_action_row: breakpoint change after creation keeps empty parts hidden",
                 "[xml][settings][setting_action_row]") {
    lv_subject_set_int(bp_, 3);
    lv_obj_t* row = make("", nullptr, nullptr);
    for (int bp : {0, 1, 3, 5, 0}) {
        CAPTURE(bp);
        lv_subject_set_int(bp_, bp);
        process_lvgl(5);
        CHECK_FALSE(shown(row, "description"));
        CHECK_FALSE(shown(row, "info_btn"));
    }
    lv_obj_delete(row);
}

TEST_CASE_METHOD(EmptyDescFixture, "setting_action_row: static description keeps its breakpoint behaviour",
                 "[xml][settings][setting_action_row]") {
    lv_subject_set_int(bp_, 3);
    lv_obj_t* row = make("Explains the row", nullptr, nullptr);
    CHECK(shown(row, "description"));
    CHECK_FALSE(shown(row, "info_btn"));
    lv_subject_set_int(bp_, 0);
    process_lvgl(5);
    CHECK_FALSE(shown(row, "description"));
    CHECK(shown(row, "info_btn"));
    lv_obj_delete(row);
}

TEST_CASE_METHOD(EmptyDescFixture, "setting_action_row: bound status never offers an empty info popup",
                 "[xml][settings][setting_action_row]") {
    lv_subject_set_int(bp_, 0);
    lv_obj_t* row = make("", "test_empty_desc_bound", "0");
    CHECK(shown(row, "status"));
    CHECK(std::string(lv_label_get_text(lv_obj_find_by_name(row, "status"))) == "0.8 mm");
    CHECK_FALSE(shown(row, "info_btn"));
    CHECK_FALSE(shown(row, "description"));
    lv_obj_delete(row);
}
```

Note: `attrs` with a `nullptr` key in the middle ends the list early. That is intentional: when `bind` is null the bind/min_bp pairs are dropped.

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[setting_action_row]'`
Expected: FAIL. `status` is not found (REQUIRE fails) and/or `description` is shown for an empty row.

- [ ] **Step 3: Implement**

Replace the middle column and the info button in `ui_xml/setting_action_row.xml` with:

```xml
      <lv_obj width="0"
              height="content" style_pad_all="0" flex_flow="column" style_pad_gap="#space_sm" flex_grow="1"
              scrollable="false" clickable="false" event_bubble="true">
        <text_body name="label" width="100%" text="$label" translation_tag="$label_tag" long_mode="wrap">
          <bind_style name="label_enabled" subject="$disabled" ref_value="0"/>
          <bind_style name="label_disabled" subject="$disabled" ref_value="1"/>
        </text_body>
        <!-- Wrappers carry emptiness; no binding writes their HIDDEN flag. -->
        <lv_obj name="description_wrap" width="100%" height="content" style_pad_all="0" style_bg_opa="0"
                style_border_width="0" scrollable="false" clickable="false" event_bubble="true"
                hidden_if_empty="$description">
          <text_small name="description" width="100%" text="$description" translation_tag="$description_tag"
                      long_mode="wrap">
            <bind_flag_if_lt subject="ui_breakpoint" flag="hidden" ref_value="$description_min_bp"/>
          </text_small>
        </lv_obj>
        <lv_obj name="status_wrap" width="100%" height="content" style_pad_all="0" style_bg_opa="0"
                style_border_width="0" scrollable="false" clickable="false" event_bubble="true"
                hidden_if_empty="$bind_description">
          <text_small name="status" width="100%" bind_text="$bind_description" long_mode="wrap">
            <bind_flag_if_lt subject="ui_breakpoint" flag="hidden" ref_value="$description_min_bp"/>
          </text_small>
        </lv_obj>
      </lv_obj>
      <!-- Info icon explains a static description; bound values have nothing to explain. -->
      <lv_obj name="info_wrap" width="content" height="content" style_pad_all="0" style_bg_opa="0"
              style_border_width="0" scrollable="false" clickable="false" event_bubble="false"
              hidden_if_empty="$description">
        <lv_obj name="info_btn"
                width="content" height="content" style_pad_all="2" style_bg_opa="0" style_border_width="0"
                clickable="true" event_bubble="false" scrollable="false">
          <bind_flag_if_ge subject="ui_breakpoint" flag="hidden" ref_value="$description_min_bp"/>
          <icon src="info_outline" size="sm" variant="muted" clickable="false" event_bubble="true"/>
          <event_cb trigger="clicked" callback="on_setting_info_clicked"/>
        </lv_obj>
      </lv_obj>
```

`bind_description` has no default in `<api>`, so an unset prop resolves empty and `hidden_if_empty` hides `status_wrap`. If the parser warns about `bind_text=""`, add `default=""` to the `bind_description` prop.

Then check `on_setting_info_clicked` (grep `src/`): if it reads the `description` label via a fixed parent/child walk, update the walk for the new `info_wrap` level. Update any C++ that looked up `"description"` on a bound row (Step's grep) to `"status"`.

- [ ] **Step 4: Run to verify it passes**

Run: `make t F='[setting_action_row]'` then `make t F='[settings]'` (the existing `test_setting_row_description_wrap.cpp` and overlay tests must stay green).
Expected: PASS.

- [ ] **Step 5: Mutation proof**

Run: `make mutate-diff MUTATE_ARGS='--tests "[setting_action_row]"'`
Expected: every hunk in `setting_action_row.xml` killed. Name the killing test in the commit body.

- [ ] **Step 6: Commit**

```bash
git add ui_xml/setting_action_row.xml tests/unit/test_setting_action_row_empty_description.cpp
git commit -m "fix(settings): an empty setting row description never shows a blank line or an empty info popup (prestonbrown/helixscreen#1023)"
git show --stat HEAD
```

---

### Task 2: Status formatters (pure)

**Files:**
- Create: `include/settings_root_status.h`, `src/ui/settings_root_status.cpp`
- Create: `tests/unit/test_settings_root_status.cpp`
- Modify: `firmware/helixscreen-esp32/components/helixapp/app_srcs.txt` (add `src/ui/settings_root_status.cpp`)

**Interfaces:**
- Produces (namespace `helix::settings::status`), all returning `std::string`:
  - `display(int brightness_pct, int sleep_sec, bool has_dimming)`
  - `appearance(bool dark, std::string_view theme_name)`
  - `sound(bool enabled, int volume_pct)`
  - `devices(int hardware_status_level)` (0 OK, 1 attention, 2 critical; `hardware_validator.h` `HardwareStatusLevel`)
  - `connection(bool ethernet_up, bool wifi_connected, std::string_view ssid)`
  - `language_time(std::string_view language_name, int time_format)` (0 = 12-hour, 1 = 24-hour)
  - `updates(int update_status, std::string_view new_version, std::string_view current_version, bool firmware_managed)` (0 Idle, 1 Checking, 2 UpdateAvailable, 3 UpToDate, 4 Error; `update_checker.h`)

- [ ] **Step 1: Write the failing test**

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_root_status.h"

#include "../catch_amalgamated.hpp"

using namespace helix::settings;

TEST_CASE("status::display", "[settings][root_status]") {
    CHECK(status::display(80, 600, true) == "80% · sleep 10 min");
    CHECK(status::display(80, 0, true) == "80% · never sleeps");
    CHECK(status::display(100, 60, false) == "Sleep 1 min");
    CHECK(status::display(100, 0, false) == "Never sleeps");
}

TEST_CASE("status::appearance", "[settings][root_status]") {
    CHECK(status::appearance(true, "Ocean") == "Dark · Ocean");
    CHECK(status::appearance(false, "Ocean") == "Light · Ocean");
    CHECK(status::appearance(true, "") == "Dark");
}

TEST_CASE("status::sound", "[settings][root_status]") {
    CHECK(status::sound(true, 60) == "Volume 60%");
    CHECK(status::sound(true, 0) == "Muted");
    CHECK(status::sound(false, 60) == "Muted");
}

TEST_CASE("status::devices", "[settings][root_status]") {
    CHECK(status::devices(0) == "All healthy");
    CHECK(status::devices(1) == "Needs attention");
    CHECK(status::devices(2) == "Problem found");
    CHECK(status::devices(7) == "All healthy");
}

TEST_CASE("status::connection", "[settings][root_status]") {
    CHECK(status::connection(true, true, "HomeNet") == "Ethernet");
    CHECK(status::connection(false, true, "HomeNet") == "Wi-Fi HomeNet");
    CHECK(status::connection(false, true, "") == "Wi-Fi");
    CHECK(status::connection(false, false, "HomeNet") == "Not connected");
}

TEST_CASE("status::language_time", "[settings][root_status]") {
    CHECK(status::language_time("English", 1) == "English · 24-hour");
    CHECK(status::language_time("Deutsch", 0) == "Deutsch · 12-hour");
}

TEST_CASE("status::updates", "[settings][root_status]") {
    CHECK(status::updates(2, "1.1.1", "1.1.0", false) == "1.1.1 available");
    CHECK(status::updates(3, "", "1.1.0", false) == "Up to date");
    CHECK(status::updates(1, "", "1.1.0", false) == "Checking…");
    CHECK(status::updates(4, "", "1.1.0", false) == "Check failed");
    CHECK(status::updates(0, "", "1.1.0", false) == "Version 1.1.0");
    CHECK(status::updates(2, "1.1.1", "1.1.0", true) == "Managed by firmware");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[root_status]'`
Expected: FAIL to compile (`settings_root_status.h` not found).

- [ ] **Step 3: Implement**

`include/settings_root_status.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

/// One-line live status shown under each Settings root row. Pure: callers pass
/// raw values, so every branch is testable without LVGL state.
namespace helix::settings::status {

std::string display(int brightness_pct, int sleep_sec, bool has_dimming);
std::string appearance(bool dark, std::string_view theme_name);
std::string sound(bool enabled, int volume_pct);
std::string devices(int hardware_status_level);
std::string connection(bool ethernet_up, bool wifi_connected, std::string_view ssid);
std::string language_time(std::string_view language_name, int time_format);
std::string updates(int update_status, std::string_view new_version, std::string_view current_version,
                    bool firmware_managed);

}  // namespace helix::settings::status
```

`src/ui/settings_root_status.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings_root_status.h"

#include "lvgl/lvgl.h"

#include <fmt/format.h>

namespace helix::settings::status {

namespace {
std::string sleep_part(int sleep_sec, bool leading) {
    if (sleep_sec <= 0) {
        return leading ? lv_tr("Never sleeps") : lv_tr("never sleeps");
    }
    return fmt::format(fmt::runtime(leading ? lv_tr("Sleep {} min") : lv_tr("sleep {} min")),
                       sleep_sec / 60);
}
}  // namespace

std::string display(int brightness_pct, int sleep_sec, bool has_dimming) {
    if (!has_dimming) {
        return sleep_part(sleep_sec, true);
    }
    return fmt::format("{}% · {}", brightness_pct, sleep_part(sleep_sec, false));
}

std::string appearance(bool dark, std::string_view theme_name) {
    const char* mode = dark ? lv_tr("Dark") : lv_tr("Light");
    if (theme_name.empty()) {
        return mode;
    }
    return fmt::format("{} · {}", mode, theme_name);
}

std::string sound(bool enabled, int volume_pct) {
    if (!enabled || volume_pct <= 0) {
        return lv_tr("Muted");
    }
    return fmt::format(fmt::runtime(lv_tr("Volume {}%")), volume_pct);
}

std::string devices(int hardware_status_level) {
    switch (hardware_status_level) {
    case 1:
        return lv_tr("Needs attention");
    case 2:
        return lv_tr("Problem found");
    default:
        return lv_tr("All healthy");
    }
}

std::string connection(bool ethernet_up, bool wifi_connected, std::string_view ssid) {
    if (ethernet_up) {
        return lv_tr("Ethernet");
    }
    if (wifi_connected) {
        return ssid.empty() ? std::string(lv_tr("Wi-Fi"))
                            : fmt::format(fmt::runtime(lv_tr("Wi-Fi {}")), ssid);
    }
    return lv_tr("Not connected");
}

std::string language_time(std::string_view language_name, int time_format) {
    return fmt::format("{} · {}", language_name, time_format == 1 ? lv_tr("24-hour") : lv_tr("12-hour"));
}

std::string updates(int update_status, std::string_view new_version, std::string_view current_version,
                    bool firmware_managed) {
    if (firmware_managed) {
        return lv_tr("Managed by firmware");
    }
    switch (update_status) {
    case 1:
        return lv_tr("Checking…");
    case 2:
        return fmt::format(fmt::runtime(lv_tr("{} available")), new_version);
    case 3:
        return lv_tr("Up to date");
    case 4:
        return lv_tr("Check failed");
    default:
        return fmt::format(fmt::runtime(lv_tr("Version {}")), current_version);
    }
}

}  // namespace helix::settings::status
```

Match the `lv_tr`/`fmt` includes used by `src/ui/ui_settings_about.cpp` (~line 405) if these differ.

- [ ] **Step 4: Run to verify it passes**

Run: `make t F='[root_status]'`
Expected: PASS (7 cases).

- [ ] **Step 5: Mutation proof and commit**

Run: `make mutate-diff MUTATE_ARGS='--tests "[root_status]"'`; every hunk killed.

```bash
git add include/settings_root_status.h src/ui/settings_root_status.cpp tests/unit/test_settings_root_status.cpp firmware/helixscreen-esp32/components/helixapp/app_srcs.txt
git commit -m "feat(settings): pure formatters for the settings root status lines (prestonbrown/helixscreen#1023)"
git show --stat HEAD
```

---

### Task 3: Split Display & Sound into four overlays

**Files:**
- Create: `include/ui_settings_display.h`, `src/ui/ui_settings_display.cpp`, `ui_xml/settings_display_overlay.xml`
- Create: `include/ui_settings_appearance.h`, `src/ui/ui_settings_appearance.cpp`, `ui_xml/settings_appearance_overlay.xml`
- Create: `include/ui_settings_sound.h`, `src/ui/ui_settings_sound.cpp`, `ui_xml/settings_sound_overlay.xml`
- Create: `include/ui_settings_language_time.h`, `src/ui/ui_settings_language_time.cpp`, `ui_xml/settings_language_time_overlay.xml`
- Delete: `include/ui_settings_display_sound.h`, `src/ui/ui_settings_display_sound.cpp`, `ui_xml/settings_display_sound_overlay.xml`
- Modify: `ui_xml/settings_touch_overlay.xml`, `src/ui/ui_panel_settings.cpp`, `ui_xml/settings_panel.xml`, `src/xml_registration.cpp`, `src/ui/ui_theme_editor_overlay.cpp` (~548, ~877), `src/ui/ui_printer_manager_overlay.cpp` (~297), `src/application/application.cpp` (~130 include), `include/screensaver_registry.h` (~53 comment), `src/remote/remote_control_server.cpp` (~1220 comment), `firmware/helixscreen-esp32/components/helixapp/app_srcs.txt`
- Tests: `tests/unit/test_subject_name_withdrawal.cpp` (~18, 40, 113-115), `test_display_rotation_setting.cpp`, `test_theme_explorer_callbacks.cpp`, `test_screensaver_registry.cpp` (~112), `test_overlay_width_push.cpp` (~191), `test_job_holds_machine.cpp` (~557), comments in `tests/helix_test_fixture.cpp` (~441) and `test_settings_subject_restoration.cpp` (~20)
- Create: `tests/unit/test_settings_page_rows.cpp` (row placement census, extended in Tasks 4-6)

**Interfaces:**
- Produces accessors (namespace `helix::settings`): `DisplaySettingsOverlay& get_display_settings_overlay()`, `AppearanceSettingsOverlay& get_appearance_settings_overlay()`, `SoundSettingsOverlay& get_sound_settings_overlay()`, `LanguageTimeSettingsOverlay& get_language_time_settings_overlay()`. Each has `init_subjects()`, `register_callbacks()`, `create(lv_obj_t*)`, `show(lv_obj_t* parent_screen)`, `on_activate()`, `get_name()`.
- Produces XML views: `settings_display_overlay`, `settings_appearance_overlay`, `settings_sound_overlay`, `settings_language_time_overlay`.
- Produces SettingsPanel callbacks: `on_display_clicked`, `on_appearance_clicked`, `on_sound_clicked`, `on_language_time_clicked` (each `get_X_settings_overlay().show(get_global_settings_panel().parent_screen_)`).
- `AppearanceSettingsOverlay::sync_explorer_to_active_theme()` stays public (theme editor calls it).

**Function map** (line numbers from `src/ui/ui_settings_display_sound.cpp` on main; move each function body unchanged, renaming only the class qualifier and the accessor its static trampoline dispatches through):

| Page | init / handlers / trampolines | Subjects | Members |
|---|---|---|---|
| Display | `init_display_rotation_dropdown` 332, `init_brightness_controls` 348, `init_dim_dropdown` 365, `init_sleep_dropdown` 381, `init_sleep_while_printing_toggle` 397, `init_ui_scale_dropdown` 416 (+ anon-namespace `kUiScalePercents` 50-53), `init_screensaver_dropdown` 490; `handle_display_rotation_changed` 699, `handle_brightness_changed` 717, `handle_brightness_commit` 726, `handle_ui_scale_changed` 744, `handle_dim_changed` 763, `handle_sleep_changed` 769, `handle_sleep_while_printing_changed` 775, `handle_test_screensaver` 1354; statics 1196, 1212, 1220, 1244, 1260, 1268, 1276, `on_screensaver_changed` 1339, `on_test_screensaver` 1348 | `brightness_value` (string, `brightness_value_buf_[8]`) | `SubjectManager subjects_`, `brightness_value_subject_` |
| Appearance | `init_animations_toggle` 309, `init_bed_mesh_dropdown` 456, `init_theme_preset_dropdown` 472; `handle_animations_changed` 680, `handle_dark_mode_changed` 711, `handle_widget_labels_changed` 731, `handle_bed_mesh_mode_changed` 757, `handle_theme_preset_changed` 780, `handle_explorer_theme_changed` 792, `handle_theme_settings_clicked` 856, `sync_explorer_to_active_theme` 893, `handle_apply_theme_clicked` 927, `handle_edit_colors_clicked` 958, `handle_preview_dark_mode_toggled` 992, `apply_preview_palette_to_screen_popups` 1020; statics 1168, 1204, 1228, 1252, 1284-1335 | `theme_apply_disabled` (int) | `theme_settings_overlay_`, `theme_explorer_overlay_`, `original_theme_index_`, `original_theme_`, `preview_is_dark_`, `preview_theme_name_`, `cached_themes_`, `subjects_`, `theme_apply_disabled_subject_` |
| Sound | `init_sounds_toggle` 509, `init_volume_slider` 527, `init_sound_theme_dropdown` 553, `init_audio_device_dropdown` 587; `handle_audio_device_changed` 633, `handle_sounds_changed` 1070, `handle_volume_commit` 1079, `handle_volume_changed` 1084, `handle_ui_sounds_changed` 1101, `handle_sound_theme_changed` 1106, `handle_preview_sounds` 1120, `handle_test_tracker` 1125; statics 650, 1373-1428 incl. `on_volume_released` | none | `volume_value_buf_[8]` |
| Language & Time | `init_language_dropdown` 256, `init_timezone_dropdown` 273, `init_time_format_dropdown` 292; `handle_language_changed` 662, `handle_timezone_changed` 668, `handle_time_format_changed` 673; statics 1144, 1152, 1160 | none | none |
| Touch (SettingsPanel statics) | `handle_system_keyboard_changed` 685, `handle_keep_navbar_changed` 690, `handle_page_scroll_buttons_changed` 736 (needs `#include "page_scroll_auto_inject.h"`); statics 1176, 1184, 1236 | none | none |
| Delete | `show_theme_preview` 1060 (no callers); SettingsPanel's `handle_sound_settings_clicked` ~879, `handle_display_settings_clicked` ~918 and their `on_sound_settings_clicked` / `on_display_settings_clicked` registrations (no XML users) | | |

`on_activate()` of each new class runs only its page's `init_*` calls from the old `on_activate` (209-250), keeping the `#ifdef HELIX_ENABLE_SCREENSAVER` (Display) and `#ifndef HELIX_HAS_TRACKER` (Sound) branches. `register_callbacks()` of each class registers only its page's names from the old list (105-155), including the no-op lambdas for screensaver callbacks when the feature is off. Copy the class skeleton (accessor with `StaticPanelRegistry::instance().register_destroy("<ClassName>", ...)`, `create`, `show`) from `src/ui/ui_settings_touch.cpp`. `get_name()` returns `"Display"`, `"Appearance"`, `"Sound"`, `"Language & Time"`.

**XML:** each new file copies `settings_display_sound_overlay.xml`'s outer `overlay_panel` shell with the page title, then one `setting_group` holding that page's rows moved verbatim (row names, subjects, binds unchanged). Display keeps the `info_note`. Sound keeps the whole `group_sound` including its `printer_has_speaker` bind. The three touch rows (`row_system_keyboard`, `row_keep_navbar`, `row_page_scroll_buttons`) move verbatim into `settings_touch_overlay.xml`'s `group_touch`, after `row_scroll_guard`.

- [ ] **Step 1: Write the failing placement test**

`tests/unit/test_settings_page_rows.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "settings_manager.h"
#include "ui_panel_settings.h"
#include "ui_update_queue.h"

#include "../catch_amalgamated.hpp"

#include <string>
#include <vector>

namespace {

struct PageRowsFixture : LVGLUITestFixture {
    lv_obj_t* root_ = nullptr;
    PageRowsFixture() {
        SettingsManager::instance().init_subjects();
        get_global_settings_panel().init_subjects();
    }
    ~PageRowsFixture() override {
        if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
        helix::ui::UpdateQueue::instance().drain();
        get_global_settings_panel().deinit_subjects();
        helix::ui::UpdateQueue::instance().drain();
    }
    void build(const char* view) {
        if (root_) lv_obj_delete(root_);
        root_ = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), view, nullptr));
        REQUIRE(root_ != nullptr);
        process_lvgl(5);
    }
    bool has(const char* row) const { return lv_obj_find_by_name(root_, row) != nullptr; }
};

struct Placement {
    const char* view;
    std::vector<const char*> rows;
};

}  // namespace

TEST_CASE_METHOD(PageRowsFixture, "settings pages: every row lives on its page", "[settings][settings_pages]") {
    const std::vector<Placement> placements = {
        {"settings_display_overlay",
         {"row_display_rotation", "row_dim", "row_display_sleep", "row_screensaver", "row_sleep_while_printing",
          "row_ui_scale", "brightness_slider"}},
        {"settings_appearance_overlay",
         {"row_dark_mode", "row_theme_settings", "row_animations", "row_widget_labels", "row_bed_mesh_mode"}},
        {"settings_sound_overlay", {"row_sounds", "row_volume", "row_sound_theme"}},
        {"settings_language_time_overlay", {"row_language", "row_timezone", "row_time_format"}},
        {"settings_touch_overlay", {"row_system_keyboard", "row_keep_navbar", "row_page_scroll_buttons"}},
    };
    for (const auto& p : placements) {
        build(p.view);
        for (const char* row : p.rows) {
            CAPTURE(p.view, row);
            CHECK(has(row));
        }
    }
}

TEST_CASE_METHOD(PageRowsFixture, "settings pages: moved rows are gone from their old page", "[settings][settings_pages]") {
    build("settings_appearance_overlay");
    CHECK_FALSE(has("row_language"));
    CHECK_FALSE(has("row_system_keyboard"));
    build("settings_display_overlay");
    CHECK_FALSE(has("row_sounds"));
}

TEST_CASE("settings pages: the Display & Sound view no longer exists", "[settings][settings_pages]") {
    CHECK(lv_xml_component_get_scope("settings_display_sound_overlay") == nullptr);
}
```

Use the exact row names from the XML (`grep -o 'name="row_[a-z_]*"' ui_xml/settings_display_sound_overlay.xml` before deleting it); the names above are the ones the inventory reported and must be corrected to match if any differ. If the fixture needs overlay `init_subjects()` for bindings to resolve, call `get_display_settings_overlay().init_subjects()` etc. in the constructor.

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[settings_pages]'`
Expected: FAIL (new views unknown).

- [ ] **Step 3: Create the four classes and XML files per the function map; move the three touch rows.**

- [ ] **Step 4: Wire SettingsPanel** (`src/ui/ui_panel_settings.cpp`)
  - Replace `get_display_sound_settings_overlay().register_callbacks()` (~429) with the four new overlays' `register_callbacks()` calls.
  - Delete the duplicate SettingsPanel registrations whose names the new overlays now own: `on_bed_mesh_mode_changed` (~380), `on_timezone_changed` (~384), `on_time_format_changed` (~385), `on_language_changed` (~386), `on_dark_mode_changed` (~395), `on_animations_changed` (~396 and ~1507), plus the SettingsPanel statics they pointed at if nothing else uses them.
  - Register `on_system_keyboard_changed`, `on_keep_navbar_changed`, `on_page_scroll_buttons_changed` once, next to the other touch statics (~388-392), not in `register_settings_panel_callbacks()`.
  - Replace `on_display_sound_clicked` (~1165) with the four `on_*_clicked` handlers from Interfaces; register them in the category block (~422-430) and in `register_settings_panel_callbacks()` (~1502-1556). Delete `on_sound_settings_clicked`/`on_display_settings_clicked` and their handlers.
  - Temporary root: in `ui_xml/settings_panel.xml`, replace `row_display_sound` with four rows `row_display`, `row_appearance`, `row_sound`, `row_language_time` (label + icon only). Task 7 rebuilds the root.

- [ ] **Step 5: Retarget external callers**
  - `ui_theme_editor_overlay.cpp` ~548, ~877: `get_appearance_settings_overlay().sync_explorer_to_active_theme()`.
  - `ui_printer_manager_overlay.cpp` ~297 (`on_chip_speaker_clicked`): `get_sound_settings_overlay().show(lv_display_get_screen_active(nullptr))`.
  - `application.cpp` ~130: include the four new headers only if used; drop the old include.
  - `src/xml_registration.cpp` (~758): replace the display_sound registration with the four new files.
  - `app_srcs.txt`: replace `src/ui/ui_settings_display_sound.cpp` with the four new files.
  - Comments naming the old file: `screensaver_registry.h` ~53, `remote_control_server.cpp` ~1220, `helix_test_fixture.cpp` ~441, `test_settings_subject_restoration.cpp` ~20.

- [ ] **Step 6: Update existing tests**
  - `test_subject_name_withdrawal.cpp`: split the DisplaySound case into Display (`brightness_value`) and Appearance (`theme_apply_disabled`).
  - `test_display_rotation_setting.cpp`: create `settings_display_overlay`; use `get_display_settings_overlay().register_callbacks()`.
  - `test_theme_explorer_callbacks.cpp`: use `get_appearance_settings_overlay()`.
  - `test_screensaver_registry.cpp` ~112: read `ui_xml/settings_display_overlay.xml`.
  - `test_overlay_width_push.cpp` ~191: `FakeOverlay("settings_display_overlay")`.
  - `test_job_holds_machine.cpp`: remove `ui_xml/settings_display_sound_overlay.xml`, add the four new files in alphabetical position.
  - Add to `test_settings_page_rows.cpp`:

```cpp
TEST_CASE_METHOD(PageRowsFixture, "settings pages: speaker chip path opens Sound", "[settings][settings_pages]") {
    helix::settings::get_sound_settings_overlay().show(lv_display_get_screen_active(nullptr));
    process_lvgl(5);
    const auto names = NavigationManager::instance().overlay_stack_names();
    REQUIRE_FALSE(names.empty());
    CHECK(names.back().find("sound") != std::string::npos);
    NavigationManager::instance().go_back();
    process_lvgl(5);
}
```

- [ ] **Step 7: Build and run**

Run: `make -j$(scripts/helix-claim jobs)` then `make t F='[settings]'` then `make t F='[job_holds_machine]'`.
Expected: build clean; all PASS.

- [ ] **Step 8: Mock check**

Launch the mock with the pinned socket; `ctl navigate settings`, then `ctl click row_display`, `ctl text` a row label, `ctl back`; repeat for `row_appearance`, `row_sound`, `row_language_time`; open Theme Colors from Appearance and back. Grep the log for `No subject was found` and `callback .* not found`: none expected. Kill the captured PID.

- [ ] **Step 9: Commit**

```bash
git add -A include/ui_settings_display.h include/ui_settings_appearance.h include/ui_settings_sound.h include/ui_settings_language_time.h src/ui/ui_settings_display.cpp src/ui/ui_settings_appearance.cpp src/ui/ui_settings_sound.cpp src/ui/ui_settings_language_time.cpp ui_xml/settings_display_overlay.xml ui_xml/settings_appearance_overlay.xml ui_xml/settings_sound_overlay.xml ui_xml/settings_language_time_overlay.xml
git rm -q include/ui_settings_display_sound.h src/ui/ui_settings_display_sound.cpp ui_xml/settings_display_sound_overlay.xml
git add ui_xml/settings_touch_overlay.xml ui_xml/settings_panel.xml src/ui/ui_panel_settings.cpp src/xml_registration.cpp src/ui/ui_theme_editor_overlay.cpp src/ui/ui_printer_manager_overlay.cpp src/application/application.cpp include/screensaver_registry.h src/remote/remote_control_server.cpp firmware/helixscreen-esp32/components/helixapp/app_srcs.txt tests/
git commit -m "refactor(settings): split Display & Sound into Display, Appearance, Sound and Language & Time pages (prestonbrown/helixscreen#1023)"
git show --stat HEAD
```

(`git add`/`git rm` are fine here: this is a private worktree, not the shared main tree.)

---

### Task 4: Connection and Updates pages

**Files:**
- Create: `include/ui_settings_connection.h`, `src/ui/ui_settings_connection.cpp`, `ui_xml/settings_connection_overlay.xml`
- Create: `include/ui_settings_updates.h`, `src/ui/ui_settings_updates.cpp`, `ui_xml/settings_updates_overlay.xml`
- Modify: `ui_xml/settings_system_overlay.xml`, `ui_xml/settings_hardware_overlay.xml`, `ui_xml/about_settings_overlay.xml`, `src/ui/ui_settings_about.cpp`, `include/ui_settings_about.h`, `src/ui/ui_panel_settings.cpp`, `src/ui/upgrade_banner.cpp` (~160), `src/system/update_checker.cpp` (~3116), `src/xml_registration.cpp`, `app_srcs.txt`, `ui_xml/settings_panel.xml` (temporary rows)
- Tests: `tests/unit/test_about_update_channel_gate.cpp` (moves to the Updates view), `test_settings_page_rows.cpp`, `test_job_holds_machine.cpp`

**Interfaces:**
- Produces: `ConnectionSettingsOverlay& get_connection_settings_overlay()` (view `settings_connection_overlay`, `get_name()` `"Connection"`), `UpdatesSettingsOverlay& get_updates_settings_overlay()` (view `settings_updates_overlay`, `get_name()` `"Updates"`, public `show_update_download_modal(bool)` and `hide_update_download_modal()` moved from About).
- Produces SettingsPanel callbacks `on_connection_clicked`, `on_updates_clicked`.

**Connection:** XML holds `container_network`/`row_network` (with its `show_network_settings` bind) and `row_printer_host` (`bind_description="printer_host_value"`, callback `on_change_host_clicked`) moved verbatim from `settings_system_overlay.xml`, and `row_printers` (callback `on_printers_clicked`) moved verbatim from `settings_hardware_overlay.xml`. The class registers no callbacks: `on_network_clicked`, `on_change_host_clicked` stay in SettingsPanel; `on_printers_clicked` stays with its current winning registrar (`HardwareSettingsOverlay`, `ui_settings_hardware.cpp` ~84-92). Delete SettingsPanel's losing duplicate `on_printers_clicked` registrations and `handle_printers_clicked` (~911) if unused. Class skeleton copies `ui_settings_touch.cpp`.

**Updates:** XML holds `container_update_channel`, `container_update_channel_dev`, `container_check_updates`, `container_install_update`, `container_updates_firmware_managed`, `container_updates_unavailable` moved verbatim from `about_settings_overlay.xml` (~77-135) with their binds. Move from `AboutSettingsOverlay` to `UpdatesSettingsOverlay`: `sync_update_channel_rows` (~505), `on_about_update_channel_changed` (~530), `on_about_check_updates_clicked` (~563), `on_about_updates_unavailable_clicked` (~570), `on_about_install_update_clicked` (~597), `show_update_download_modal` (~356), `hide_update_download_modal` (~411), `on_update_download_start/cancel/dismiss` (~630-645), and their registrations (~146-157). Keep callback names unchanged. `UpdatesSettingsOverlay::on_activate()` calls `sync_update_channel_rows()`. About keeps the 7-tap `row_version` beta toggle; after the toggle writes `show_beta_features` (~484), call `get_updates_settings_overlay().sync_update_channel_rows()` only if the Updates overlay is created (`is_created()`).

**Retarget:**
- `upgrade_banner.cpp` ~160 `on_update_clicked`: keep `set_active(PanelId::Settings)`, then `helix::settings::get_updates_settings_overlay().show(lv_display_get_screen_active(nullptr))` (the same parent the printer-manager speaker chip uses; `SettingsPanel::parent_screen_` is protected).
- `update_checker.cpp` ~3116 `on_update_notify_install`: `get_updates_settings_overlay().show_update_download_modal(true)`.

**System / Hardware slimming:** remove `container_network`/`row_network`, `row_printer_host`, `row_touch_input` from `settings_system_overlay.xml`; remove `row_printers` from `settings_hardware_overlay.xml`. `on_touch_input_clicked` stays registered (root uses it in Task 7).

**Temporary root:** add `row_connection` and `row_updates` rows to `settings_panel.xml`, and `row_touch_input` (callback `on_touch_input_clicked`) so Touch & Input stays reachable.

- [ ] **Step 1: Failing tests.** Extend `test_settings_page_rows.cpp` placements:

```cpp
        {"settings_connection_overlay", {"row_network", "row_printer_host", "row_printers"}},
        {"settings_updates_overlay",
         {"row_update_channel", "row_update_channel_dev", "row_check_updates", "row_install_update",
          "row_updates_unavailable"}},
```

and the absence case:

```cpp
TEST_CASE_METHOD(PageRowsFixture, "settings pages: connection and update rows left their old pages",
                 "[settings][settings_pages]") {
    build("settings_system_overlay");
    CHECK_FALSE(has("row_network"));
    CHECK_FALSE(has("row_printer_host"));
    CHECK_FALSE(has("row_touch_input"));
    build("settings_hardware_overlay");
    CHECK_FALSE(has("row_printers"));
    build("about_settings_overlay");
    CHECK_FALSE(has("row_check_updates"));
    CHECK(has("row_version"));
}
```

Move `test_about_update_channel_gate.cpp`'s fixture to create `settings_updates_overlay` and call `get_updates_settings_overlay().init_subjects()` (rename the file to `test_updates_channel_gate.cpp`; keep its assertions). `UpgradeBanner::on_update_clicked` is a private static, so the banner path is verified in the Step 5 mock check (show the banner with `HELIX_MOCK_*` update env per `docs/devel/MOCK_ENVIRONMENT_VARIABLES.md`, `ctl click` its update button, `ctl ls` shows `row_check_updates`).

- [ ] **Step 2: Run to verify fail:** `make t F='[settings_pages]'` -> FAIL.
- [ ] **Step 3: Implement** per the sections above; register both XML files in `src/xml_registration.cpp`; add both `.cpp` to `app_srcs.txt`; add both XML files to `kNoMachineControlFiles`; register both overlays' `register_callbacks()` in `SettingsPanel::init_subjects()` after the Task 3 calls; add `on_connection_clicked`/`on_updates_clicked` to the category block and `register_settings_panel_callbacks()`.
- [ ] **Step 4: Run:** `make -j$(scripts/helix-claim jobs)`, `make t F='[settings]'`, `make t F='[update]'`, `make t F='[job_holds_machine]'` -> PASS.
- [ ] **Step 5: Mock check:** upgrade banner button lands on Updates (see Step 1 note); navigate settings > Connection (Network opens, Host modal opens and cancels, Printers list opens), settings > Updates (Check for Updates row present), Help & About > About (no update rows, version tap still works). No `callback .* not found` in the log.
- [ ] **Step 6: Commit**

```bash
git add include/ui_settings_connection.h include/ui_settings_updates.h src/ui/ui_settings_connection.cpp src/ui/ui_settings_updates.cpp ui_xml/settings_connection_overlay.xml ui_xml/settings_updates_overlay.xml ui_xml/settings_system_overlay.xml ui_xml/settings_hardware_overlay.xml ui_xml/about_settings_overlay.xml ui_xml/settings_panel.xml src/ui/ui_settings_about.cpp include/ui_settings_about.h src/ui/ui_panel_settings.cpp src/ui/upgrade_banner.cpp src/system/update_checker.cpp src/xml_registration.cpp firmware/helixscreen-esp32/components/helixapp/app_srcs.txt tests/
git commit -m "feat(settings): Connection and Updates pages; Network, Host and Printers gather in Connection, update controls leave Help > About (prestonbrown/helixscreen#1023)"
git show --stat HEAD
```

---

### Task 5: Printing sections, Appearance printer visuals, filament rows

**Files:**
- Modify: `ui_xml/settings_printing_overlay.xml`, `src/ui/ui_settings_printing.cpp` (+ header), `ui_xml/settings_appearance_overlay.xml`, `src/ui/ui_settings_appearance.cpp` (+ header), `ui_xml/settings_safety_overlay.xml`, `src/ui/ui_settings_safety.cpp`, `src/ui/ui_panel_settings.cpp`
- Tests: `test_settings_page_rows.cpp`

**Moves:**
- `row_toolhead_style`, `row_gcode_mode`, `row_z_movement_style` XML (settings_printing_overlay.xml ~21-46) move to a new `setting_group` in `settings_appearance_overlay.xml` headed `<setting_group_header title="PRINTER VISUALS" title_tag="PRINTER VISUALS"/>`, followed by `row_bed_mesh_mode` (move it from the first group into this one).
- C++ moves from `PrintingSettingsOverlay` to `AppearanceSettingsOverlay` unchanged: `init_toolhead_style_dropdown` (~153), `init_gcode_mode_dropdown` (~174, with its GLES index remap), `init_z_movement_dropdown` (~205), `handle_toolhead_style_changed` (~226), `handle_gcode_mode_changed` (~233), `handle_z_movement_style_changed` (~247), trampolines (~273/281/289) and their registrations (~74-76). Appearance `on_activate()` calls the three inits.
- Delete SettingsPanel's losing duplicates `on_toolhead_style_changed`, `on_gcode_mode_changed`, `on_z_movement_style_changed` (registrations ~376-419, statics ~150/160/170).
- `row_allow_cold_extrude` and `row_filament_auto_cooldown` XML (settings_safety_overlay.xml ~58-72) move to Printing. Their callbacks `on_allow_cold_extrude_changed`, `on_filament_auto_cooldown_changed` and handlers (`ui_settings_safety.cpp` ~229, ~234, trampolines ~305, ~313, registrations ~80-81) move to `PrintingSettingsOverlay`.
- Printing XML becomes three `setting_group`s with headers MACHINE (`row_machine_limits`, `row_motion`, `row_retraction` container, `row_enclosure_style`), FILAMENT (`row_material_temps`, `row_allow_cold_extrude`, `row_filament_auto_cooldown`), EXTRAS (`row_timelapse` container, `row_macro_buttons`). Use the exact names in the file.

- [ ] **Step 1: Failing tests** in `test_settings_page_rows.cpp`:

```cpp
TEST_CASE_METHOD(PageRowsFixture, "settings pages: printer visuals live in Appearance, filament rows in Printing",
                 "[settings][settings_pages]") {
    build("settings_appearance_overlay");
    CHECK(has("row_toolhead_style"));
    CHECK(has("row_gcode_mode"));
    CHECK(has("row_z_movement_style"));
    build("settings_printing_overlay");
    CHECK_FALSE(has("row_toolhead_style"));
    CHECK(has("row_allow_cold_extrude"));
    CHECK(has("row_filament_auto_cooldown"));
    build("settings_safety_overlay");
    CHECK_FALSE(has("row_allow_cold_extrude"));
}

TEST_CASE_METHOD(PageRowsFixture, "Appearance fills the printer-visual dropdowns on activate",
                 "[settings][settings_pages]") {
    auto& page = helix::settings::get_appearance_settings_overlay();
    page.show(test_screen());
    process_lvgl(5);
    lv_obj_t* row = lv_obj_find_by_name(lv_screen_active(), "row_toolhead_style");
    REQUIRE(row != nullptr);
    lv_obj_t* dd = lv_obj_find_by_name(row, "dropdown");
    REQUIRE(dd != nullptr);
    CHECK(lv_dropdown_get_option_count(dd) > 1);  // XML ships a lone "Auto" placeholder
    NavigationManager::instance().go_back();
    process_lvgl(5);
}
```

- [ ] **Step 2:** `make t F='[settings_pages]'` -> FAIL.
- [ ] **Step 3:** Implement the moves.
- [ ] **Step 4:** `make -j$(scripts/helix-claim jobs)`, `make t F='[settings]'` -> PASS. Mock: change Toolhead Style in Appearance, confirm the home toolhead icon changes; toggle Cool nozzle after filament ops in Printing.
- [ ] **Step 5: Commit**

```bash
git add ui_xml/settings_printing_overlay.xml ui_xml/settings_appearance_overlay.xml ui_xml/settings_safety_overlay.xml src/ui/ui_settings_printing.cpp include/ui_settings_printing.h src/ui/ui_settings_appearance.cpp include/ui_settings_appearance.h src/ui/ui_settings_safety.cpp src/ui/ui_panel_settings.cpp tests/unit/test_settings_page_rows.cpp
git commit -m "refactor(settings): printer visuals move to Appearance, filament behaviour to Printing, Printing gains sections (prestonbrown/helixscreen#1023)"
git show --stat HEAD
```

---

### Task 6: Devices and Safety & Alerts renames

**Files:**
- Modify: `ui_xml/settings_hardware_overlay.xml` (title), `include/ui_settings_hardware.h` (`get_name()` ~33), `ui_xml/settings_safety_overlay.xml` (title), `include/ui_settings_safety.h` (`get_name()` ~51, `@brief` ~6), `include/ui_settings_display_sound.h` is gone; update any other `@brief` naming old pages.
- Tests: `test_settings_page_rows.cpp`

View names (`settings_hardware_overlay`, `settings_safety_overlay`) and class names stay: renaming them is churn with no user-visible effect. Only titles and `get_name()` change.

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("settings pages: renamed page titles", "[settings][settings_pages]") {
    CHECK(std::string(helix::settings::get_hardware_settings_overlay().get_name()) == "Devices");
    CHECK(std::string(helix::settings::get_safety_settings_overlay().get_name()) == "Safety & Alerts");
}
```

- [ ] **Step 2:** FAIL. **Step 3:** change the two `get_name()` returns and the two overlay title attributes (`title="Devices" title_tag="Devices"`, `title="Safety &amp; Alerts" title_tag="Safety &amp; Alerts"`). **Step 4:** `make t F='[settings_pages]'` PASS.
- [ ] **Step 5: Commit** `git commit -m "feat(settings): Hardware & Devices becomes Devices, Safety & Notifications becomes Safety & Alerts (prestonbrown/helixscreen#1023)"` with explicit paths; `git show --stat HEAD`.

---

### Task 7: Grouped-list root

**Files:**
- Modify: `ui_xml/settings_panel.xml`, `src/ui/ui_panel_settings.cpp`
- Create: `tests/unit/test_settings_root.cpp`

**Interfaces:**
- Consumes: all `on_*_clicked` handlers from Tasks 3-4; existing `on_printing_clicked`, `on_safety_clicked`, `on_system_clicked`, `on_help_clicked`, `on_touch_input_clicked`; `on_hardware_clicked` renamed `on_devices_clicked`.
- Produces root row names: `row_display`, `row_appearance`, `row_touch_input`, `row_sound`, `row_printing`, `row_devices`, `row_safety`, `row_connection`, `row_language_time`, `row_system`, `row_updates`, `row_help`; groups `group_screen`, `group_printer`, `group_helixscreen`.

Before editing: check whether `fix/controls-tools-row-fit` has merged (`git branch --merged main | grep controls-tools-row-fit`). If not and it is claimed LIVE, message its owner first (global CLAUDE.md § Peer Sessions). This task only uses `setting_group` and `setting_group_header`, it does not edit them.

- [ ] **Step 1: Failing test** `tests/unit/test_settings_root.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "settings_manager.h"
#include "ui_panel_settings.h"
#include "ui_update_queue.h"

#include "../catch_amalgamated.hpp"

#include <string>
#include <vector>

namespace {
struct RootFixture : LVGLUITestFixture {
    lv_obj_t* root_ = nullptr;
    RootFixture() {
        SettingsManager::instance().init_subjects();
        get_global_settings_panel().init_subjects();
        root_ = static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "settings_panel", nullptr));
        REQUIRE(root_ != nullptr);
        process_lvgl(5);
    }
    ~RootFixture() override {
        if (root_ && lv_obj_is_valid(root_)) lv_obj_delete(root_);
        helix::ui::UpdateQueue::instance().drain();
        get_global_settings_panel().deinit_subjects();
        helix::ui::UpdateQueue::instance().drain();
    }
    lv_obj_t* find(const char* n) const { return lv_obj_find_by_name(root_, n); }
    void set_int(const char* subject, int v) {
        lv_subject_t* s = lv_xml_get_subject(nullptr, subject);
        REQUIRE(s != nullptr);
        lv_subject_set_int(s, v);
        process_lvgl(5);
    }
};
}  // namespace

TEST_CASE_METHOD(RootFixture, "settings root: three groups, twelve rows, in order", "[settings][settings_root]") {
    const std::vector<std::pair<const char*, std::vector<const char*>>> groups = {
        {"group_screen", {"row_display", "row_appearance", "row_touch_input", "row_sound"}},
        {"group_printer", {"row_printing", "row_devices", "row_safety", "row_connection"}},
        {"group_helixscreen", {"row_language_time", "row_system", "row_updates", "row_help"}},
    };
    for (const auto& [group, rows] : groups) {
        lv_obj_t* g = find(group);
        REQUIRE(g != nullptr);
        int last_index = -1;
        for (const char* r : rows) {
            CAPTURE(group, r);
            lv_obj_t* row = lv_obj_find_by_name(g, r);
            REQUIRE(row != nullptr);
            int idx = lv_obj_get_index(row);
            CHECK(idx > last_index);
            last_index = idx;
        }
    }
    CHECK(find("row_display_sound") == nullptr);
    CHECK(find("row_hardware") == nullptr);
}

TEST_CASE_METHOD(RootFixture, "settings root: Sound hides without a speaker", "[settings][settings_root]") {
    set_int("printer_has_speaker", 0);
    CHECK(lv_obj_has_flag(find("row_sound"), LV_OBJ_FLAG_HIDDEN));
    set_int("printer_has_speaker", 1);
    CHECK_FALSE(lv_obj_has_flag(find("row_sound"), LV_OBJ_FLAG_HIDDEN));
}

TEST_CASE_METHOD(RootFixture, "settings root: Updates hides only when no update row would show",
                 "[settings][settings_root]") {
    set_int("show_update_settings", 0);
    set_int("updates_firmware_managed", 0);
    set_int("updates_unavailable", 0);
    CHECK(lv_obj_has_flag(find("row_updates"), LV_OBJ_FLAG_HIDDEN));
    set_int("updates_firmware_managed", 1);
    CHECK_FALSE(lv_obj_has_flag(find("row_updates"), LV_OBJ_FLAG_HIDDEN));
    set_int("updates_firmware_managed", 0);
    set_int("show_update_settings", 1);
    CHECK_FALSE(lv_obj_has_flag(find("row_updates"), LV_OBJ_FLAG_HIDDEN));
}
```

If `printer_has_speaker` is not registered by these inits, register it in the fixture the way `test_about_update_channel_gate.cpp` registers `show_beta_features`.

- [ ] **Step 2:** `make t F='[settings_root]'` -> FAIL.

- [ ] **Step 3: Implement** `ui_xml/settings_panel.xml` view body:

```xml
    <setting_section_header title="SETTINGS" title_tag="SETTINGS" icon="settings"/>
    <setting_group name="group_screen">
      <setting_group_header title="SCREEN" title_tag="SCREEN"/>
      <setting_action_row name="row_display" label="Display" label_tag="Display" icon="light"
                          callback="on_display_clicked"/>
      <setting_action_row name="row_appearance" label="Appearance" label_tag="Appearance" icon="palette"
                          callback="on_appearance_clicked"/>
      <setting_action_row name="row_touch_input" label="Touch &amp; Input" label_tag="Touch &amp; Input"
                          icon="cursor_move" callback="on_touch_input_clicked"/>
      <setting_action_row name="row_sound" label="Sound" label_tag="Sound" icon="volume_high"
                          callback="on_sound_clicked">
        <bind_flag_if_eq subject="printer_has_speaker" flag="hidden" ref_value="0"/>
      </setting_action_row>
    </setting_group>
    <setting_group name="group_printer">
      <setting_group_header title="PRINTER" title_tag="PRINTER"/>
      <setting_action_row name="row_printing" label="Printing" label_tag="Printing" icon="printer_3d"
                          callback="on_printing_clicked"/>
      <setting_action_row name="row_devices" label="Devices" label_tag="Devices" icon="sysinfo"
                          callback="on_devices_clicked"/>
      <setting_action_row name="row_safety" label="Safety &amp; Alerts" label_tag="Safety &amp; Alerts"
                          icon="alert_octagon" callback="on_safety_clicked"/>
      <setting_action_row name="row_connection" label="Connection" label_tag="Connection" icon="lan"
                          callback="on_connection_clicked"/>
    </setting_group>
    <setting_group name="group_helixscreen">
      <setting_group_header title="HELIXSCREEN" title_tag="HELIXSCREEN"/>
      <setting_action_row name="row_language_time" label="Language &amp; Time" label_tag="Language &amp; Time"
                          icon="translate" callback="on_language_time_clicked"/>
      <setting_action_row name="row_system" label="System" label_tag="System" icon="settings"
                          callback="on_system_clicked"/>
      <setting_action_row name="row_updates" label="Updates" label_tag="Updates" icon="download"
                          callback="on_updates_clicked">
        <bind_flag_if cond="show_update_settings eq 0 and updates_firmware_managed eq 0 and updates_unavailable eq 0"
                      flag="hidden"/>
      </setting_action_row>
      <setting_action_row name="row_help" label="Help &amp; About" label_tag="Help &amp; About" icon="help"
                          callback="on_help_clicked"/>
    </setting_group>
```

Keep the existing bottom spacer. Confirm a `bind_flag_*` child inside a component instance applies to the instance root (it does for `container_*` wrappers elsewhere; if not, wrap the row in a transparent `lv_obj` carrying the bind, the pattern `settings_system_overlay.xml` uses for `container_network`). Rename `on_hardware_clicked` to `on_devices_clicked` in `ui_panel_settings.cpp` (handler + both registration sites). Update the file's header comment ("Six category rows") to describe three groups.

- [ ] **Step 4:** `make -j$(scripts/helix-claim jobs)`, `make t F='[settings]'` -> PASS. Mock: every root row opens its page and back returns to the root.
- [ ] **Step 5: Commit** `git commit -m "feat(settings): grouped settings root with Screen, Printer and HelixScreen sections (prestonbrown/helixscreen#1023)"` with explicit paths; `git show --stat HEAD`.

---

### Task 8: Live status lines

**Files:**
- Modify: `include/ui_panel_settings.h`, `src/ui/ui_panel_settings.cpp`, `ui_xml/settings_panel.xml`, `include/system_settings_manager.h`, `src/system/system_settings_manager.cpp` (path per repo; `LANGUAGE_CODES`/`LANGUAGE_OPTIONS_TEXT` ~46-49)
- Test: `tests/unit/test_settings_root.cpp`

**Interfaces:**
- Consumes: `helix::settings::status::*` (Task 2).
- Produces: `SettingsPanel::on_activate() override` calling `refresh_status_lines()`; public `void refresh_status_lines();`; seven string subjects `settings_status_display`, `settings_status_appearance`, `settings_status_sound`, `settings_status_devices`, `settings_status_connection`, `settings_status_language_time`, `settings_status_updates` (registered in `SettingsPanel::init_subjects()`, withdrawn in `deinit_subjects()`, 64-byte buffers, same macro as `printer_host_value`); `std::string SystemSettingsManager::get_language_display_name() const` (native name of the current language from `LANGUAGE_OPTIONS_TEXT`).

**Sources** (from the inventory):

| Status | Read |
|---|---|
| display | `settings_brightness` (int %), `settings_display_sleep` (int s), `settings_has_dimming` |
| appearance | `settings_dark_mode`, `DisplaySettingsManager::instance().get_theme_name()` |
| sound | `settings_sounds_enabled`, `settings_volume` |
| devices | `get_hardware_status_level_subject()` int |
| connection | `EthernetManager` `get_info()`/`has_interface()` for wired-up; `get_wifi_manager()` may be null: then `wifi_connected=false`; else `is_connected()`, `get_connected_ssid()` |
| language_time | `SystemSettingsManager::instance().get_language_display_name()`, `settings_time_format` |
| updates | `update_status`, `update_new_version` (string), `update_current_version` (string, registered by About; if absent use the build version constant About uses), `updates_firmware_managed` |

Read subjects with `lv_xml_get_subject(nullptr, name)` and guard null (a subject owned by an uncreated overlay may not exist yet): a missing subject yields the formatter's neutral input (0 / false / "").

- [ ] **Step 1: Failing tests** (append to `test_settings_root.cpp`):

```cpp
namespace {
std::string status_text(lv_obj_t* root, const char* row) {
    lv_obj_t* r = lv_obj_find_by_name(root, row);
    REQUIRE(r != nullptr);
    lv_obj_t* s = lv_obj_find_by_name(r, "status");
    REQUIRE(s != nullptr);
    return lv_label_get_text(s);
}
}  // namespace

TEST_CASE_METHOD(RootFixture, "settings root: status lines follow values on return", "[settings][settings_root]") {
    set_int("settings_sounds_enabled", 1);
    set_int("settings_volume", 40);
    get_global_settings_panel().refresh_status_lines();
    process_lvgl(5);
    CHECK(status_text(root_, "row_sound") == "Volume 40%");

    set_int("settings_volume", 0);
    CHECK(status_text(root_, "row_sound") == "Volume 40%");  // no observer: stale until return
    get_global_settings_panel().on_activate();
    process_lvgl(5);
    CHECK(status_text(root_, "row_sound") == "Muted");
}

TEST_CASE_METHOD(RootFixture, "settings root: rows without state show no status", "[settings][settings_root]") {
    for (const char* row : {"row_touch_input", "row_printing", "row_safety", "row_system", "row_help"}) {
        CAPTURE(row);
        lv_obj_t* r = find(row);
        REQUIRE(r != nullptr);
        lv_obj_t* wrap = lv_obj_find_by_name(r, "status_wrap");
        REQUIRE(wrap != nullptr);
        CHECK(lv_obj_has_flag(wrap, LV_OBJ_FLAG_HIDDEN));
    }
}

TEST_CASE_METHOD(RootFixture, "settings root: refresh survives a missing Wi-Fi manager", "[settings][settings_root]") {
    // helix-tests builds have no Wi-Fi manager unless a test installs one.
    get_global_settings_panel().refresh_status_lines();
    process_lvgl(5);
    const std::string s = status_text(root_, "row_connection");
    CHECK((s == "Not connected" || s == "Ethernet"));
}

TEST_CASE("SystemSettingsManager names the current language natively", "[settings][settings_root]") {
    CHECK_FALSE(SystemSettingsManager::instance().get_language_display_name().empty());
}
```

If the audio subjects are owned by `AudioSettingsManager` and not initialized by the fixture, call its `init_subjects()` in `RootFixture` (the base fixture's destructor re-inits it, `tests/helix_test_fixture.cpp` ~437-460).

- [ ] **Step 2:** `make t F='[settings_root]'` -> FAIL (`refresh_status_lines` missing).
- [ ] **Step 3: Implement**
  - Header: `void on_activate() override;` and `void refresh_status_lines();`, seven `lv_subject_t` + `char[64]` buffers.
  - `on_activate()`: call `PanelBase::on_activate()` then `refresh_status_lines()`.
  - `refresh_status_lines()`: gather per the sources table, call the formatters, `lv_subject_copy_string()` each result into its subject.
  - `get_language_display_name()`: split `LANGUAGE_OPTIONS_TEXT` on `\n` and return the entry at the current `settings_language` index; empty-safe.
  - XML: add to the seven stateful root rows `bind_description="settings_status_<x>" description_min_bp="0"`.
- [ ] **Step 4:** `make t F='[settings_root]'` and `make t F='[root_status]'` -> PASS.
- [ ] **Step 5: Mutation proof:** `make mutate-diff MUTATE_ARGS='--tests "[settings_root]"'`; hunks in `refresh_status_lines` and `on_activate` killed.
- [ ] **Step 6: Mock check:** open settings: every stateful row shows a status; change brightness in Display, back: Display's status shows the new percent; change language to Deutsch in Language & Time, back: labels and statuses are German.
- [ ] **Step 7: Commit** `git commit -m "feat(settings): live status under Display, Appearance, Sound, Devices, Connection, Language & Time and Updates (prestonbrown/helixscreen#1023)"` with explicit paths; `git show --stat HEAD`.

---

### Task 9: Translations and in-app path strings

**Files:**
- Modify: `translations/*.yml`, `ui_xml/translations/*.xml` (generated)
- Modify: `src/application/application.cpp` (~4433 "Settings > About", ~4560 "Settings > Display"), `src/ui/ui_wizard_telemetry.cpp` (~65), `src/ui/ui_wizard_summary.cpp` (~294), `ui_xml/components/upgrade_banner.xml` (~12 comment)

- [ ] **Step 1:** Fix in-app path strings to the new locations: debug bundle is under **Help & About**; the ~4560 string points at the page now holding the setting it names (read the surrounding code: Display or Appearance); telemetry viewer is **System > View Telemetry Data**. Keep them `lv_tr()`-wrapped. Update the upgrade banner comment to Updates.
- [ ] **Step 2:** `make translation-sync` then fill every empty placeholder in all 8 non-English locales for: the new labels (Display, Sound, Devices, Connection, Updates, Language & Time, Safety & Alerts, SCREEN, PRINTER, HELIXSCREEN, PRINTER VISUALS, MACHINE, FILAMENT, EXTRAS), every status string from Task 2, and the changed path strings. Reuse terms from `translations/GLOSSARY.md` and existing translations (e.g. "Connection" already exists in all locales). The category label "Printing" is a settings category, not a state: it/es/ja/zh must read as the category (e.g. it "Stampa", es "Impresión", ja "印刷", zh "打印"); if the existing key is shared with the print-in-progress state, give the root row a distinct key via `label_tag` context per `docs/devel/TRANSLATION_SYSTEM.md`.
- [ ] **Step 3:** `make translations`; run `scripts/check_cjk_font_staleness.sh` (via `make translation-sync` output) and regenerate CJK fonts if it asks.
- [ ] **Step 4:** `make t F='[i18n]'` and `make t F='[translation]'` -> PASS; the quality check `qc_translation_coverage` passes on commit.
- [ ] **Step 5: Commit** `git commit -m "i18n(settings): translate the reorganized settings pages and status lines; in-app hints point at the new pages (prestonbrown/helixscreen#1023)"` with explicit paths incl. `translations/` and `ui_xml/translations/`; `git show --stat HEAD`.

---

### Task 10: Docs pass, scripts, screenshots

**Files:** everything in the spec's "Docs pass" section, plus:
- `scripts/screenshot-recipes.sh` (~38, 73-85), `scripts/screenshot-all.sh` (~32-53), `scripts/screenshot.sh` (~103), `scripts/generate-screenshots.sh` (~37), `scripts/screensaver-perf/pi3b_loadgate.sh`, `scripts/screensaver-perf/pi3b_measure.sh`
- `tests/ui/test_state.py`, `tests/ui/test_text.py`

- [ ] **Step 1: Scripts and UI tests.** Recipe chains become:

```
display            navigate settings; click row_display
theme              navigate settings; click row_appearance; click row_theme_settings
sensors            navigate settings; click row_devices; click row_filament_sensors
network            navigate settings; click row_connection; click row_network
hardware-health    navigate settings; click row_devices; click row_hardware_health
fan-settings       navigate settings; click row_devices; click row_fan_settings
barcode-scanner    navigate settings; click row_devices; click row_spoolman_settings; click row_barcode_scanner
label-printer      navigate settings; click row_devices; click row_spoolman_settings; click row_label_printer
security           navigate settings; click row_system; click row_security
safety             navigate settings; click row_safety
help-about         navigate settings; click row_help
help-qr            navigate settings; click row_help; click row_discord; click btn_ok
```

Add recipes `appearance`, `sound`, `touch-input`, `connection`, `language-time`, `updates`, `printing` following the same shape. The pi3b scripts click `row_display` instead of `row_display_sound`. `tests/ui/test_state.py` clicks `row_appearance` to find `row_widget_labels`. Run `bats tests/shell/` files that cover screenshot recipes (grep `screenshot-recipes` in `tests/shell/`).

- [ ] **Step 2: User guide restructure.** `docs/user/guide/settings.md` becomes the three-group index (table per group: page, what's in it, link). Create `docs/user/guide/settings/{display,appearance,sound,language-time,connection,updates,devices}.md` from the matching sections of `display-sound.md`, `hardware.md`, `system.md`, `help-about.md`; update `printing.md`, `safety.md`, `system.md`, `help-about.md`, `touch-input.md` for their new contents; delete `display-sound.md` and `hardware.md`; update `led-settings.md`'s breadcrumb to Devices. Fix `docs/user/USER_GUIDE.md` (~85) and `docs/user/CLAUDE.md` routing table (~42-50).
- [ ] **Step 3: Sweep** every file in the spec's list (floor: 60 files, 198 mentions) for old names and paths. Anchors: `CONFIGURATION.md` ~519/532 link `display-sound.md#scroll-buttons`/`#ui-scale`; retarget to `touch-input.md#scroll-buttons` and `display.md#ui-scale`. Leave `PRIVACY_POLICY.md` and `TELEMETRY.md` edits as a separate, clearly-labelled hunk for Preston's review; do not reword beyond the path.
- [ ] **Step 4: Devel docs and comments** listed in the spec (HELIXCTL, SOUND_SYSTEM, THEME_CONTRIBUTOR_GUIDE, architecture/10, ENVIRONMENT_VARIABLES, printers/SNAPMAKER_U1_SUPPORT; header comments in `ui_settings_touch.h`, `ui_change_host_modal.h`, `overlay_class.h`, `ui_nav_manager.h`, `camera_widget.h`, `ui_wizard_input_shaper.h`).
- [ ] **Step 5: Changelog.** Add to `docs/devel/CHANGELOG_1_1_DRAFT.md` (follow its section format) a Settings entry with a short old-to-new table: Display & Sound -> Display / Appearance / Sound / Language & Time; Hardware & Devices -> Devices (Printers -> Connection); Safety & Notifications -> Safety & Alerts (filament rows -> Printing); System > Network, Host -> Connection; System > Touch & Input -> root; Help & About > About > updates -> Updates.
- [ ] **Step 6: Gate grep.** Must print nothing except the changelog table:

```bash
grep -rnE 'Display (&|and|&amp;) Sound|Hardware (&|and|&amp;) Devices|Safety (&|and|&amp;) Notifications|display-sound\.md|settings/hardware\.md|row_display_sound|row_hardware\b|on_display_sound_clicked|on_hardware_clicked|settings_display_sound_overlay|Settings ?(>|→|->|›) ?(About|System ?(>|→|->|›) ?(Network|Printer Host|Touch)|Hardware)' docs/ src/ include/ ui_xml/ scripts/ tests/ README.md | grep -v CHANGELOG_1_1_DRAFT
```

- [ ] **Step 7: Screenshots.** Build, then `scripts/screenshot-all.sh` for the settings recipes; replace `docs/images/screenshot-settings-panel.png` and any settings page shots the guide references. Open every regenerated PNG and confirm it shows the intended page before committing.
- [ ] **Step 8:** `make check-doc-anchors` (advisory) and let the commit hook run `check_doc_refs.py`.
- [ ] **Step 9: Commit** in two commits: `docs(settings): user guide and references follow the reorganized settings (prestonbrown/helixscreen#1023)` (docs + images) and `chore(settings): screenshot recipes, perf scripts and UI tests follow the new settings rows (prestonbrown/helixscreen#1023)` (scripts + tests/ui). `git show --stat HEAD` after each.

---

### Task 11: Verification and follow-ups

- [ ] **Step 1:** `make full-test-run`. All green; if a shard fails, read it before re-running (a peer build in the same tree deletes the binary: check `scripts/helix-claim list`).
- [ ] **Step 2: `ctl` walk** with the pinned-socket mock at `--size micro`, `medium`, `large`, `480x800`: from the root open every page, `ctl ls` it, open every action row one level down, `ctl back` to the root. Log must contain no `No subject was found`, `callback .* not found`, or `parameter is not defined`. Capture one root screenshot per size and open each PNG.
- [ ] **Step 3:** Root in German and Russian (set language in Language & Time, return, screenshot at medium and micro; labels wrap at word boundaries, statuses translated).
- [ ] **Step 4:** Docs gate grep (Task 10 Step 6) clean.
- [ ] **Step 5:** Delete both plan files (`docs/devel/plans/2026-09-27-settings-ia-reorg*.md`) in the final commit, moving any durable knowledge (the three-group model, the status-line rule) into `docs/devel/UI_CONTRIBUTOR_GUIDE.md`'s settings section.
- [ ] **Step 6: File follow-up issues** (Backlog, `gh issue create`): multi-column grid root gated on measured label fit; merge Printers and Host; status observers (only if stale values are reported).
- [ ] **Step 7:** Hand back for merge review (independent review per branch, then merge to main and `scripts/teardown-worktree.sh 1023-settings-ia`).

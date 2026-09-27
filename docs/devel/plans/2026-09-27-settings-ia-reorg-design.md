# Settings Information Architecture Reorg - Design

Issue: prestonbrown/helixscreen#1023 (milestone 1.1)
Status: design approved in brainstorm 2026-09-27; implementation plan not yet written.
Evidence: real-app mockup renders at six sizes, two languages and portrait, published privately as the "Grouped Settings Root" artifact.

## Goals

1. **Findability.** A new user can guess where a setting lives from the root alone. Labels name what is inside, not an umbrella ("Display & Sound", "System").
2. **Room to grow.** 1.1 keeps adding rows. Each page has one concern, so a new row has an obvious home and no page becomes the next catch-all.

Not a goal: making the root fit on one screen at every size. Today's 6-row root already scrolls at 800x480 and 480x400.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| Scope | Whole tree, all six current categories | The worst findability misses (Updates buried in Help > About, Printers vs Host split) are outside Display & Sound |
| Shape | 12 pages, one tap from the root, in 3 visual groups | Concrete labels over umbrella hubs; no page gains a tap |
| Root presentation | Grouped single-column list | Works in every language, orientation and size. A multi-column grid failed on 480-wide screens, portrait and German compounds in the mockup |
| Root second line | Live status where a row has real state; nothing otherwise | Apple and Bambu both do this; a content-list hint does not fit and is redundant with concrete labels |
| Status refresh | Recomputed in `SettingsPanel::on_activate()`; no observers | Nearly every source changes only inside a sub-page, and returning refreshes. Revisit if stale values bite |
| C++ | Split `DisplaySoundSettingsOverlay` into four classes | 1431 lines, four concerns; per-row `init_*`/`handle_*` already split along the seams |

## The tree

`*` = conditional row (existing visibility rule unchanged). Row `name=` attributes are preserved when a row moves.

### SCREEN

| Page | Rows | Moves in |
|---|---|---|
| **Display** | Brightness*, Screen Dim*, Display Sleep, Sleep While Printing, Screensaver, Test Screensaver*, Screen Rotation*, UI Scale | from Display & Sound |
| **Appearance** | Dark Mode, Theme Colors, Animations, Widget Labels; section PRINTER VISUALS: Toolhead Style, G-code Preview, Z Movement, Bed Mesh Render | from Display & Sound; Toolhead Style, G-code Preview, Z Movement from Printing |
| **Touch & Input** | Touch Calibration*, Show Touch Points, Scroll Engage Distance, Long Press Time, Scroll Guard, Scroll Buttons, Allow Home Screen Editing, System Keyboard*, Keep Navigation Bar* | page from System; Scroll Buttons, System Keyboard, Keep Navigation Bar from Display & Sound |
| **Sound*** | unchanged rows and visibility rules; the root row is hidden when `printer_has_speaker` is 0 | from Display & Sound |

### PRINTER

| Page | Rows | Moves in |
|---|---|---|
| **Printing** | MACHINE: Machine Limits, Motion, Retraction*, Enclosure. FILAMENT: Material Temperatures, Allow cold load/unload, Cool nozzle after filament ops. EXTRAS: Timelapse*, Macro Buttons | the two filament rows from Safety |
| **Devices** | Hardware Health, Camera*, Multi-Filament System*, Fans*, Sensors*, LED Settings*, Power Devices*, Spoolman (Label Printer, Barcode Scanner nested) | renamed from Hardware & Devices; Printers moves out |
| **Safety & Alerts** | E-Stop Confirmation, Cancel Escalation, Escalation Timeout*, Confirm before running macros, Spaghetti Detection*, Pause on Detection*, Print Completion Alert, On-screen Alerts | renamed from Safety & Notifications |
| **Connection** | Network Settings*, Printers, Host | new; Network and Host from System, Printers from Hardware |

### HELIXSCREEN

| Page | Rows | Moves in |
|---|---|---|
| **Language & Time** | Language, Timezone, Time Format | from Display & Sound |
| **System** | Security, Performance*, Share Usage Data, View Telemetry Data*, Log Level, Restart HelixScreen, Factory Reset | loses Network, Host, Touch & Input |
| **Updates*** | Update Channel (both variants, existing gates), Check for Updates, Install Update, Software Updates rows | from Help > About. The root row is hidden when every update row would be hidden (firmware-managed, unavailable) |
| **Help & About** | Replay Welcome Tour, Upload Debug Bundle, Discord Community, Documentation, About (info rows, Print Hours) | loses its update rows |

## Root

- Three `setting_group` cards, each opening with a `setting_group_header` (SCREEN, PRINTER, HELIXSCREEN), holding full-width `setting_action_row`s. No new layout code.
- Root row names: `row_display`, `row_appearance`, `row_touch_input`, `row_sound`, `row_printing`, `row_devices`, `row_safety`, `row_connection`, `row_language_time`, `row_system`, `row_updates`, `row_help`.
- Rows with status bind `bind_description` to a string subject and set `description_min_bp="0"` so the status shows at every breakpoint. Rows without status have no description.

### Status lines

| Row | Examples | Sources |
|---|---|---|
| Display | `80% · sleep 10 min`, `Sleep off`, `Sleep 10 min` (no dimming) | brightness, `display_sleep_sec`, `settings_has_dimming` |
| Appearance | `Dark · Ocean`, `Light · Ocean` | dark mode, active theme name |
| Sound | `Volume 60%`, `Muted` | `settings_sounds_enabled`, volume |
| Devices | `All healthy`, `1 warning`, `2 problems` | Hardware Health's existing status |
| Connection | `Wi-Fi HomeNet`, `Ethernet`, `Not connected` | `connected_ssid`, network state |
| Language & Time | `English · 24-hour` | `settings_language`, `settings_time_format` |
| Updates | `Up to date`, `1.1.1 available` | `update_status`, `update_available` |

- One pure function per row maps raw values to a translated string (`lv_tr()`). No LVGL dependency, so every branch is unit-testable.
- `SettingsPanel::on_activate()` recomputes all of them and sets the string subjects. Updates, Devices and Connection can go stale while the root stays on screen; accepted for now.
- Status text is short by construction; the list is full width, so no truncation logic.

### `setting_action_row` fix

An empty description must collapse the row to one line and never show the (i) info icon. Today `bind_flag_if_lt`/`bind_flag_if_ge` on `ui_breakpoint` clear the `hidden` flag that `hidden_if_empty` set, on both the description label and the info button. The fix makes emptiness win at every breakpoint. This is a latent bug for any description-less row, fixed as its own commit.

## C++ shape

- `DisplaySoundSettingsOverlay` becomes `DisplaySettingsOverlay`, `AppearanceSettingsOverlay`, `SoundSettingsOverlay`, `LanguageTimeSettingsOverlay`. Each row's `init_*`/`handle_*`/`on_*` moves unchanged to the class owning its page. Theme preview/editor entry points go with Appearance.
- Scroll Buttons, System Keyboard and Keep Navigation Bar handlers move to `TouchSettingsOverlay`.
- New `ConnectionSettingsOverlay` and `UpdatesSettingsOverlay` follow the singleton pattern of `src/ui/ui_settings_touch.cpp`.
- Toolhead Style, G-code Preview, Z Movement init/handlers move from the Printing overlay to Appearance; the two filament toggles move from Safety to Printing.
- New view names: `settings_display_overlay`, `settings_appearance_overlay`, `settings_sound_overlay`, `settings_language_time_overlay`, `settings_connection_overlay`, `settings_updates_overlay`. `settings_display_sound_overlay.xml` and its class are deleted.
- **Callbacks.** XML event callbacks are registered by name into a global table, last write wins, and about a dozen names are already registered twice (`SettingsPanel` plus an overlay). Every moved callback ends with exactly one registrar. The split commit lists the names it moves and verifies no duplicates among them.
- Overlay titles (`get_name()`, e.g. `"Display & Sound"`, `"Hardware & Devices"`, `"Safety & Notifications"`) follow the new page names.

## Out of scope

- Multi-column grid root (follow-up issue: grid only when every label fits its column, measured, not by breakpoint).
- Merging Printers and Host (follow-up issue).
- Status observers (follow-up issue, filed only if refresh-on-return proves stale in use).
- Declarative XML port of these overlays (#1143). The split makes it easier; doing both at once doubles the change.

## Migration

- **Settings:** no `settings.json` key changes. Users keep every value.
- **Deep links:** `scripts/screenshot-recipes.sh` (12 recipes), `scripts/screenshot-all.sh`, `scripts/screenshot.sh`, `scripts/generate-screenshots.sh`, `scripts/screensaver-perf/pi3b_loadgate.sh` and `pi3b_measure.sh`, `tests/ui/test_state.py`, `tests/ui/test_text.py`. Only chain prefixes change, since moved rows keep their names.
- **Translations:** new keys for the new labels, group headers and status strings. New label keys also correct "Printing", which it/es/ja/zh translate as print-in-progress ("Stampa in corso", "Imprimiendo", "印刷中", "正在打印"). `make translation-sync` leaves empty placeholders; fill all locales, then `make translations`.

## Tests

- **Update:** unit tests pinning rows to overlay files (`test_display_rotation_setting`, `test_screensaver_registry`, `test_overlay_width_push`, `test_detection_settings_rows`, `test_setting_dropdown_row_bind_selected`, `test_settings_hardware_health_row`), the settings XML list in `test_job_holds_machine.cpp`, and fixture helpers that name overlay files (`tests/helix_test_fixture.cpp`, `test_settings_subject_restoration.cpp`, `input_settings_test_helpers.h`, `home_edit_mode_test_helpers.h`).
- **New:** status formatters, every branch; root structure (3 groups, 12 rows, Sound hidden without a speaker, Updates hidden when updates are unavailable/firmware-managed); every moved row present in its new overlay; `setting_action_row` empty description collapses and hides the info icon at every breakpoint.
- **Proof:** `make mutate-diff` on the formatter and row-fix hunks, named in the commit bodies.

## Docs pass

This is large and gets its own commits. A floor measurement: **at least 198 mentions of the old names in 60 files** (explicit "Display & Sound", "Hardware & Devices", "Safety & Notifications", "Touch & Input", "Help & About", "Settings > X" paths, and links to the settings sub-pages). Other spellings exist, so the sweep greps wider than that.

1. **User guide restructure.** `docs/user/guide/settings.md` becomes the index of 3 groups and 12 pages. `docs/user/guide/settings/` becomes one file per page: `display.md`, `appearance.md`, `touch-input.md`, `sound.md`, `printing.md`, `devices.md` (from `hardware.md`, keeps linking `led-settings.md`), `safety.md`, `connection.md`, `language-time.md`, `system.md`, `updates.md`, `help-about.md`. `display-sound.md` and `hardware.md` are removed; fix every inbound link.
2. **User doc sweep.** Heaviest: `CONFIGURATION.md` (34), `TROUBLESHOOTING.md` (14), `FAQ.md` (12), `docs/user/CLAUDE.md` (8), `INSTALL.md` (7), then getting-started, filament, filament-tracking, barcode-scanner, touch-calibration, tips, home-panel, UPGRADING, security, motion, label-printing, fans, calibration, bluetooth-setup, advanced, supported-printers, temperature.
3. **Privacy and telemetry text.** `PRIVACY_POLICY.md` and `TELEMETRY.md` tell users where the telemetry toggle and data viewer live. Wording changes there are reviewed by Preston, not swept.
4. **In-app path strings** (translated, user-facing): `src/application/application.cpp` ("Settings > About" in the debug-bundle toast, "Settings > Display"), `src/ui/ui_wizard_telemetry.cpp` and `src/ui/ui_wizard_summary.cpp` ("Settings > View Telemetry"), `ui_xml/components/upgrade_banner.xml` ("Settings > About").
5. **Devel docs and code comments.** `HELIXCTL.md`, `SOUND_SYSTEM.md`, `THEME_CONTRIBUTOR_GUIDE.md`, `architecture/10-theme-tokens-layout.md`, `ENVIRONMENT_VARIABLES.md`, `printers/SNAPMAKER_U1_SUPPORT.md`; path comments in `include/ui_settings_touch.h`, `include/ui_change_host_modal.h`, `include/overlay_class.h`, `include/ui_nav_manager.h`, `src/ui/panel_widgets/camera_widget.h`.
6. **Screenshots.** Regenerate settings screenshots (`docs/images/screenshot-settings-panel.png` and any page shots) through `scripts/screenshot-all.sh` once the recipes are updated.
7. **Changelog.** Entry in `docs/devel/CHANGELOG_1_1_DRAFT.md` telling existing users where things went (a short old-to-new table).
8. **Gate.** Before merge, a grep for every old page name and old `Settings > X` path across `docs/`, `src/`, `include/`, `ui_xml/` and `scripts/` returns nothing except the changelog's old-to-new table.

## Phasing

MAJOR work: one worktree branch, merged to main as one change, so no 1.1 build ships a half-moved tree. Commit order:

1. `setting_action_row` empty-description fix + test.
2. Split Display & Sound into Display / Appearance / Sound / Language & Time; move the three rows into Touch & Input.
3. Connection and Updates pages; row moves into Printing, Appearance, Devices; slim System and Help & About.
4. Grouped-list root with new row names.
5. Status formatters + subjects + tests.
6. Translations (keys, all locales), in-app path strings.
7. Docs pass (items 1-7), scripts and deep links, screenshots.

## Verification before merge

- `ctl` walk opening every page and every row at micro (480x272), medium (800x480), large (1024x600) and portrait (480x800), plus the root in German and Russian.
- Docs gate grep (docs pass item 8) returns clean.
- `make full-test-run`.
- File the follow-up issues listed under Out of scope.

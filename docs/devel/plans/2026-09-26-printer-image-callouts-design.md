# Printer image live callouts: design

prestonbrown/helixscreen#1397. Design agreed 2026-09-26; supersedes the open questions in the issue body.

## Goal

The home-panel printer image widget shows what the printer is doing, on the picture of the
printer: live temperature, fan and light chips pinned to the part they describe, with leader
lines when the widget has room for them. Reference point is the Bambu-style print screen: a
drawing of the printer with `34°C` in the chamber, `80°C` under the bed and `250°C` on the
nozzle.

An idle printer shows a clean image. A chip appearing is the signal that something started.

## Decisions

| Question | Decision |
|---|---|
| Live values or state only? | Live values (`205/220°`, `80%`) |
| When does a chip show? | Only while active (table below). Never a permanent readout |
| Leader lines or pinned chips? | Both, chosen by free space around the fitted image (mode rule below) |
| 1×1 cell | Image only |
| Untagged images | Same chips, docked, no lines. Every printer gets chips, so two users with different printers see the same feature |
| Where do points come from? | Hand-tagged per image with `tools/printer-regions-tagger.html`, in telemetry-popularity order. No guessed geometry |
| Storage | One file, `assets/images/printers/regions.json`, keyed by image basename |
| Helpers | Reuse the existing colour / temperature / formatting helpers. Anything missing is discussed first (three gaps were; resolutions below) |

## Behaviour

### Chips

| Chip | Shows when | Text | Look |
|---|---|---|---|
| Nozzle | target > 0, or residual heat after turn-off | `205/220°` heating, `220°` at target | heating colour + pulse, at-temp colour: the temp tiles' rule |
| Bed | same | same | same, plus a warm glow over the bed while heating |
| Chamber | effective target > 0 (printers with a chamber heater) | same | same, via the chamber-mode classifier |
| Part fan | `fan_speed` > 0 | `80%` | spinning fan icon, speed-scaled |
| Light | `led_state` on (gated on `printer_has_led`) | icon only | lit bulb |

**Residual heat:** after a heater's target returns to 0, its chip stays, greyed, until the part
cools below 50°C, as a still-hot warning. It reads the current temperature only (`64°`).

**Nozzle + fan merge:** in pinned mode the tagged nozzle and fan points sit ~8px apart at 2×2,
so they render as one toolhead chip (`220° ✣80%`). In leader-line modes they stay separate.

### Mode rule

Evaluated on every resize, whenever the set of active chips changes, and on image change:

1. **Image only** at 1×1 cell size.
2. **Both sides**: tagged image, and each side of the centred image fits a chip column. Each part
   goes to the side nearer its point. Preferred over one side when both fit: shorter lines, and
   the image stays centred.
3. **One side**: tagged image, and the total free width fits one column. The image moves to the
   left; all chips stack in the right-hand band (the 4×2 case).
4. **Pinned**: tagged image, no usable band. Chips sit on their points, no lines.
5. **Docked**: untagged image. Chips stack in the free band if one exists, else along the
   bottom edge, with no lines.

"Fits a chip column" means the band is at least the widest active chip plus the outer margin
plus a minimum leader run, so a line is always visible. A column rung applies only if the column
also fits its chips stacked vertically; otherwise the next rung is tried. "1×1 cell size" is the
span the grid granted (`on_size_changed` colspan and rowspan of one cell), not a pixel guess. A widget taller than the image's aspect runs the same rule with bands above
and below.

### Taps

| Target | Opens |
|---|---|
| Nozzle / bed / chamber chip | temp graph overlay, `Mode::Nozzle` / `Bed` / `Chamber` |
| Fan chip | fan control overlay |
| Light chip | LED control overlay |
| Anywhere else | Printer Manager (unchanged) |

## Data

`assets/images/printers/regions.json`, one line per image:

```json
"creality-k1c": {"size": [1601, 1204], "nozzle": [0.513, 0.279], "part_fan": [0.488, 0.206],
                 "chamber": [0.313, 0.379], "light": [0.321, 0.164],
                 "bed": [[0.308, 0.571], [0.611, 0.573]]}
```

- Points are normalized 0..1 over the source PNG. The prerendered tier `.bin` is an
  aspect-preserving resize of that PNG (`scripts/lib/lvgl_image_lib.sh#lvgl_render_image`,
  `--resize-fit`), and the exact-size cache contain-fits it centred on a transparent canvas, so
  one set of numbers holds at every size.
- `bed` is the plate's near edge as seen in the picture: left end, right end. It sizes the glow
  and places the bed chip.
- `part_fan`, `chamber`, `light` are optional (skipped when the image has none).
- `size` is the source PNG's width and height. A unit test fails, naming the image, when a PNG no
  longer matches. That guards against `scripts/trim_printer_images.sh` re-cropping a tagged
  image and silently shifting its points.
- Loaded once via `helix::asset_path("assets/images/printers/regions.json")` + `json::parse`, the
  way `PrinterDatabase::load()` reads its file. Ships with every deploy and release unchanged
  (`DEPLOY_ASSET_DIRS`, `RELEASE_ASSETS`). ESP32 stages this directory itself
  (`scripts/esp32_stage_assets.py`) and needs the file added; check the Android asset manifest
  (`scripts/check_platform_manifest.py`) too.

13 images tagged at design time, covering the top of the telemetry ranking: AD5X, Voron 2.4,
AD5M Pro, K2 Plus, Trident, K1 Max, K1C, U1, Voron 0.2, Q2, K1, Creator 5 Pro, SV08.

## Architecture

| Unit | Job | Built on |
|---|---|---|
| `PrinterImageRegions` | parse `regions.json`; `lookup(basename)` → optional points | `asset_path`, `hv/json.hpp` |
| `compute_callout_layout()` | **pure function**. In: widget w×h, cell span, image aspect, optional points, active chips with their text sizes. Out: mode, image rect, each chip's rect, each leader line's points | shape of `BedCoordMapper` (`include/bed_coord_mapper.h`); no LVGL |
| residual heat | `is_residual_hot(current_deci)` and a named 50°C constant | next to `classify_heat_state()` in `include/ui_temperature_utils.h`; existing callers unchanged |
| `activity_chip.xml` | icon + bound text + colour variant | design tokens; `status_pill` / `beta_badge` styling |
| `PrinterImageWidget` | observes the subjects; classifies; formats; publishes per-widget chip text/state subjects the XML binds; positions chips and lines from the layout result | existing observers and deferred timer |

This is the measure / decide / publish / bind pattern in `docs/devel/PANEL_WIDGET_GUIDE.md`
(`NozzleTempsWidget` is its exemplar), and follows its rules:

- Chip text is measured with the same function that composes it for display, so the layout
  never decides on a string the chip does not draw. Measure the values actually showing, and
  keep a comfort margin on every text-budgeted threshold: an exact fit renders as an overlap.
- The mode and each chip's state/variant are published as widget subjects registered through
  `register_widget_subjects()`, so they exist before the XML that binds them parses. The XML
  binds visibility, variant and lines-shown off them.
- Chip x/y and line points are the one imperative part: geometry XML cannot bind, set from the
  layout result.

Existing helpers reused:

- State and colour: `classify_heater_status()`, `classify_heat_state[_with_mode]()`,
  `get_heating_state_color()`, `displayed_deci()`, `DEFAULT_AT_TEMP_TOLERANCE_DECI`
  (`include/ui_temperature_utils.h`). Per-surface status subjects are the existing pattern
  (`controls_*_status_state`), so the widget owns its own.
- Text: `format_temperature`, `format_temperature_pair`.
- Animation: `HeaterIconBinder` (`include/ui_heater_icon_binder.h`) for the heating pulse;
  `fan_spin_start` / `fan_spin_stop` (`include/ui/fan_spin_animation.h`), honouring the
  animations preference like `fan_stack_widget.cpp#update_fan_animation`.
- Subjects: `extruder_temp`/`extruder_target`, `bed_temp`/`bed_target`, `chamber_temp`/
  `chamber_effective_target`/`chamber_mode`, `fan_speed`, `led_state`, `printer_has_led`,
  `printer_has_chamber_heater`. All temperatures are decidegrees.
- Navigation: `get_global_temp_graph_overlay().open(mode, parent)` as in
  `heater_temp_widget.cpp#handle_temp_clicked`; the fan and LED overlay open sequences in
  `FanWidget::handle_clicked` and `LedControlsWidget::handle_clicked`. Chip taps are XML
  `<event_cb>`, not `lv_obj_add_event_cb`.

### Rendering and perf

- Chips are XML instances on a callout layer inside `printer_container` with
  `LV_OBJ_FLAG_IGNORE_LAYOUT`, positioned from the layout result (geometry is `DECLARATIVE_OK`
  territory; text and state stay bound).
- Leader lines are up to five `lv_line` objects on the same layer. No canvas.
- The bed glow is a translucent ellipse whose opacity pulses. No shadow or blur: too slow on the
  MIPS boards.
- Chip sizes come from font text metrics, never from forcing layout.
- Every relayout goes through the widget's existing one-shot deferred timer
  (`printer_image_widget.cpp#schedule_image_refresh`). Nothing forces layout during a grid
  rebuild (#983, #1025).
- The widget does not currently override `on_size_changed`; it will, and it must also relayout
  from `attach()` because widget instances are recycled across rebuilds.
- Image aspect comes from `lv_image_decoder_get_info()` on the tier source (precedent:
  `src/helix_splash.cpp`), not the exact-size cache, whose header is always the widget size.

## Phases

1. **Pinned and docked chips.** `size` in the tagger and data; `PrinterImageRegions` + PNG-size
   guard test; `is_residual_hot`; `activity_chip`; `compute_callout_layout()` with image-only,
   pinned and docked modes; widget wiring and taps; rewrite the stale
   `assets/images/printers/README.md` (13 of 85 images documented) to cover the format and the
   tagger; ESP32 staging entry. Shippable alone.
2. **Leader lines.** Both-sides and one-side modes, `lv_line` leaders, bed glow.
3. **Stretch: in-app tagger.** Settings → Printer → Tag printer image: full-screen overlay with
   the same six prompts, Skip and Undo, a review step with live chips, saved to
   `<config>/printer_image_regions.json`, which overrides the shipped entry per image. Reset
   returns to shipped points. The only way a custom photo gets pinned chips.

## Testing

- `compute_callout_layout()`: every mode; each threshold at its exact boundary; the
  column-too-short fallback; tall widgets (bands above/below); nozzle+fan merge; untagged dock;
  1×1.
- `PrinterImageRegions`: missing file, malformed JSON, unknown image, optional keys absent.
- Guard: every `regions.json` entry's `size` matches its PNG header.
- `is_residual_hot`: above, at and below the threshold.
- Widget: XML fixture tests driving mock subjects (heating, at target, residual heat, fan, light,
  no LED capability) asserting chip visibility, text, variant and tap routing.
- Live: mock run resized via `ctl` to 1×1, 2×2, 4×2 and 4×3 at 480×272, 800×480 and 1024×600,
  positions checked with `ctl geom`; screenshot recipes in `scripts/screenshot-recipes.sh`.
- `make mutate-diff` on the new tests.

## Tagging workflow

```bash
python3 -m http.server -d . 8000
# open http://localhost:8000/tools/printer-regions-tagger.html
```

Six taps per image: nozzle tip, part fan, bed near-left, bed near-right, an empty spot inside the
enclosure, the light. Fan, chamber and light can be skipped. A review step renders chips at the
points before saving. Progress is kept in the browser; the page's output replaces
`regions.json`. Images are listed in telemetry-popularity order (`POPULAR` at the top of the
tool), then alphabetically.

## Parked

Print-progress bar along the image's bottom edge; active spool colour swatch (#1264); Klippy-error
outline; contributing user-tagged points back via debug bundles.

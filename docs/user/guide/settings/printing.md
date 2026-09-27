# Settings: Printing

The Printing category contains settings that affect how prints are configured and displayed.

---

## Toolhead Style

Choose the toolhead icon shown on the Home Panel and Print Status screen. Options:

| Option | Description |
|--------|-------------|
| **Auto** (default) | HelixScreen detects your toolhead from the printer database or Klipper config |
| **Stealthburner** | Voron StealthBurner toolhead |
| **A4T** | Armored Turtle toolhead |
| **AntHead** | AntHead toolhead |
| **JabberWocky** | JabberWocky toolhead |

Most users can leave this on **Auto**. Change it if HelixScreen picks the wrong icon or if you've swapped to an aftermarket toolhead.

> **Note:** The native styles (**Default**, **Creality K1**, **Creality K2**) are auto-detected from your printer and don't appear as choices in the dropdown.

---

## G-code Preview

Choose how the G-code of the active print is visualized:

| Option | Description |
|--------|-------------|
| **Auto** (default) | HelixScreen picks the best mode for your hardware — interactive 3D on capable devices, falling back to lighter modes on slower ones |
| **3D View** | Interactive 3D rendering of the toolpath |
| **2D Layers** | Flat per-layer view — lighter on the GPU than 3D |
| **Thumbnail Only** | Shows just the slicer-embedded thumbnail, no live toolpath rendering — the lightest option |

Use a lighter mode if your hardware struggles with 3D rendering.

---

## Z Movement

Controls how Z-axis movement is displayed in the motion controls.

| Mode | Behavior |
|------|----------|
| **Auto** (default) | HelixScreen auto-detects based on your printer type (bed-slinger vs CoreXY vs delta) |
| **Bed Moves** | Z controls labeled as bed movement (bed goes down = nozzle moves up relative to bed) |
| **Nozzle Moves** | Z controls labeled as nozzle movement (nozzle goes up = away from bed) |

This only changes the direction labels in the UI — the actual G-code sent is the same. Use this if auto-detection picks the wrong style for your printer.

---

## Enclosure

Tells HelixScreen whether your printer is enclosed. It decides whether [Dry Filament on the bed](../temperature.md#drying-filament-on-the-bed) is offered, which needs an enclosure.

| Mode | Behavior |
|------|----------|
| **Auto** (default) | Enclosed when your printer model is known to ship enclosed, or when a chamber heater is configured |
| **Enclosed** | Treat the printer as enclosed. Use this if you enclosed it yourself |
| **Open frame** | Treat the printer as open. Dry Filament stays hidden |

---

## Machine Limits

Tap to open the Machine Limits overlay. A banner at the top reminds you: **"Changes are temporary and reset on printer reboot."** These sliders override your Klipper config for the current session — useful for testing or troubleshooting motion issues. To make permanent changes, edit `printer.cfg` directly.

Each setting has a slider, with the current value shown on the right in a tappable field:

> **Tap the value to type an exact number.** Sliders are quick but coarse — Max Acceleration spans 500–50000 over a few hundred pixels, so landing on exactly 3000 by dragging is luck. Tapping the value opens a numeric keypad where you type the number you actually want.

| Setting | Range | Description |
|---------|-------|-------------|
| **Max Velocity** | 50–1000 mm/s | Maximum toolhead speed |
| **Max Acceleration** | 500–50000 mm/s² | Maximum acceleration |
| **Accel to Decel** | 500–50000 mm/s² | Acceleration-to-deceleration limit (caps how aggressively moves slow down) |
| **Square Corner Velocity** | 1.0–20.0 mm/s | Maximum speed carried through square corners. Accepts half steps (5.5) — type them on the keypad. |
| **Extrude Speed** | 1–50 mm/s | Feedrate used for manual extrude/retract actions |

> **Extrude Speed is saved.** Unlike the motion limits above, the Extrude Speed value is a persisted HelixScreen setting — it is remembered across restarts and applies to the manual extrude/retract controls.

Below the adjustable sliders is a read-only **Config-defined** section showing your **Max Z Velocity** and **Max Z Accel**, which come from your Klipper config and cannot be changed here.

**Reset:** the **Reset** button at the bottom restores the motion limits to your printer's original configured values.

---

## Retraction Settings

> Only shown when firmware retraction (`[firmware_retraction]`) is configured in Klipper.

Tap to open the retraction overlay. Configure G10/G11 firmware retraction parameters. **Changes apply immediately** and can be adjusted mid-print for tuning.

As on Machine Limits, each value is shown in a tappable field — **tap it to type an exact number** on the numeric keypad instead of hunting for it with the slider. The distances accept two decimals (0.85 mm).

| Parameter | Range | Description |
|-----------|-------|-------------|
| **Enable Retraction** | On/Off | Master toggle for firmware retraction |
| **Retract Length** | 0.00–6.00 mm | Amount of filament to retract. 0.4–2 mm for direct drive, 4–6 mm for bowden. |
| **Retract Speed** | 10–80 mm/s | Speed of the retraction movement |
| **Unretract Extra** | 0.00–1.00 mm | Extra filament to prime after retraction to compensate for ooze |
| **Unretract Speed** | 10–60 mm/s | Speed of the prime (unretract) movement |

---

## Material Temperatures

Configure preheat presets for different filament materials (PLA, PETG, ABS, TPU, etc.). Each material can have:

- **Nozzle Temperature** - Target extruder temperature. The field clamps to the range your printer and nozzle allow, so it cannot be set past what the hardware supports.
- **Bed Temperature** — Target bed temperature
- **Preheat Macro** — A Klipper macro to run when preheating this material
- **Macro Handles Heating** — If enabled, the macro is responsible for setting temperatures. If disabled, HelixScreen sets temperatures first, then runs the macro as an additional step.

### Editing materials and brands by hand

Everything on this screen and in the filament catalog (the brand and material picker) can also be edited in one file, `user_filaments.json`. On most installs it lives in `~/printer_data/config/helixscreen/`, so Mainsail or Fluidd can open it from their config file browser. The copy in the install folder's `config/` directory (for example `~/helixscreen/config/`) is a link to the same file.

The file has two lists: `types` for material types (PLA, PETG and so on) and `filaments` for branded products.

```json
{
  "types": [
    {"name": "PLA", "nozzle_min": 205, "nozzle_max": 225, "bed": 65},
    {"name": "ABS", "chamber": 50, "preheat_macro": "PREHEAT_ABS", "macro_handles_heating": true},
    {"name": "PEKK", "nozzle_min": 330, "nozzle_max": 360, "bed": 120, "chamber": 80}
  ],
  "filaments": [
    {"id": "mybrand-pla-basic", "brand": "MyBrand", "name": "PLA Basic", "type": "PLA",
     "nozzle": 215, "nozzle_min": 200, "nozzle_max": 230, "bed": 60}
  ]
}
```

**Material types (`types`).** An entry whose `name` matches a built-in material changes only the fields you list; everything else keeps the built-in value. The Material Temperatures screen writes here too, so a change you make on the screen shows up in this file and the other way round. An entry with a new `name` adds a material type, which then appears in the Material Temperatures list and can be used as a product's `type`.

- Fields: `nozzle_min`, `nozzle_max`, `bed`, `chamber` (0 means no chamber heat), `preheat_macro`, `macro_handles_heating`, `dry_temp`, `dry_time` (minutes), `density` (g/cm³), `category` and `compat_group`.
- Numbers must be written as plain numbers: `205`, not `"205"`. A field whose value is text or `null` is ignored with a warning in the log, and the built-in value stays.
- A new type must set `nozzle_min` and `nozzle_max`, with `nozzle_max` at least `nozzle_min`; a type without a usable nozzle range is skipped with a warning in the log. Set `bed` too; anything else it leaves out is 0. Its `compat_group` defaults to its own name, so endless spool never swaps it with a different material. Set `compat_group` to an existing group (for example `"PLA"`) if it really is interchangeable with that group.
- The built-in list is the `types` section of `assets/filaments.json` in the HelixScreen install folder. Read it for names and default values, but don't edit it, because updates replace it.
- **Reset to Default** on the Material Temperatures screen removes your temperature and macro fields for a built-in type. A type you added has no default, so the button is hidden for it.

**Brands and products (`filaments`).**

- `id`, `brand`, `name` and `type` are required. The `id` must be unique; lowercase with dashes is the convention.
- `nozzle`, `nozzle_min`, `nozzle_max`, `bed` and `density` are optional. Anything you leave out comes from the product's material type, including your changes in `types`.
- To change a built-in product, use its `id` and include only the fields you want to change. Built-in products are in the `filaments` section of `assets/filaments.json`.
- Temperatures set on a product win over its type's values.

Restart HelixScreen after editing so every screen picks up the change. A file that is only a list of products (`[ ... ]`) still works; HelixScreen rewrites it in the form above the next time it saves. If the file stops parsing, HelixScreen ignores it until it's fixed, and the next save from the screen (a product or a Material Temperatures change) copies it to `user_filaments.json.bak` before starting fresh.

Before this file was linked into `printer_data`, it lived only in the install folder, where an update from Mainsail or Fluidd deletes it. If you added products on an older version, copy the file somewhere safe before updating.

**Material temperatures from older versions.** Older versions kept Material Temperatures changes in `settings.json` under `material_overrides`. HelixScreen moves them into `types` in `user_filaments.json` the first time it starts, and removes them from `settings.json`. If `user_filaments.json` doesn't parse at that point, they stay in `settings.json` and the move is tried again on the next start. Going back to an older version loses them, because older versions only look in `settings.json`. Which material each preset button uses stays in `settings.json` under `preset_materials`, a list of four entries such as `{"type": "PETG"}`; stop HelixScreen before editing `settings.json`, because it rewrites the whole file whenever it saves a setting.

---

## Motion

Jog speeds and the per-mode move distances for the jog pad. See [Motion](../motion.md#motion-settings) for what each setting does.

---

## Timelapse

> Only shown when the [Moonraker-Timelapse](https://github.com/mainsail-crew/moonraker-timelapse) plugin is installed.

Tap to open the Timelapse Settings overlay. Configure how HelixScreen records timelapse videos of your prints.

| Setting | Options | Description |
|---------|---------|-------------|
| **Enable Timelapse** | On/Off | Master toggle for timelapse recording |
| **Recording Mode** | Layer / Hyperlapse | **Layer** captures one frame at each layer change — best for most prints. **Hyperlapse** captures frames at fixed time intervals — better for very long prints. |
| **Framerate** | 15 / 24 / 30 / 60 fps | Playback speed of the rendered video. 30 fps is the default. |
| **Auto-render video** | On/Off | When enabled, automatically renders a video file when the print completes |

A quick **timelapse toggle** also appears on the print status panel, so you can enable or disable recording without leaving the print view.

Changes are saved immediately and sent to Moonraker. If the timelapse plugin is not yet installed, HelixScreen shows an **Install Wizard** that walks you through the SSH commands to set it up — see [Advanced > Timelapse](../advanced.md#timelapse) for details.

For browsing and playing recorded videos, see **Advanced > Timelapse Videos** (covered in [Advanced > Timelapse](../advanced.md#timelapse)).

---

## Macro Buttons

Tap to open the Macro Buttons overlay. Configure quick-action buttons and standard macro assignments.

### Quick Buttons

The Controls panel's **Quick Actions** card has four quick buttons under the Home row. Each one runs one of the [standard actions](#standard-macros) below, or toggles the printer light:

| Setting | Default |
|---------|---------|
| **Quick Button 1** | Clean Nozzle |
| **Quick Button 2** | Bed Level |
| **Quick Button 3** | (Empty) |
| **Quick Button 4** | (Empty) |

- **A standard action** runs whatever macro that action is assigned to under Standard Macros. If your printer has no macro for it, the button shows greyed out.
- **Light** turns the button into an on/off switch for the printer lights, the same one as the home screen's LED Light widget. It hides while no light is controllable.
- **(Empty)** hides the button.

While a light is controllable and no Quick Button is set to **Light**, the first button you have never set that would otherwise be empty shows the light, and its dropdown reads **Light**. Picking **(Empty)** for that button turns the light off there and keeps it off.

**Cool Down** (on the Preheat widget while a heater is on, and on the Filament panel) runs the `cooldown` G-code from `settings.json`. By default it turns off the extruder and bed heaters. Override it when cooling down should do more, such as turning off a chamber heater or bed fans:

```
SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0
SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0
SET_FAN_SPEED FAN=bed_fan SPEED=0
```

See the [default_macros reference](../../CONFIGURATION.md#default_macros) for the format.

### Standard Macros

HelixScreen auto-detects common macros from your Klipper configuration (e.g., it recognizes `CLEAN_NOZZLE`, `NOZZLE_CLEAN`, and similar naming patterns). You can override any auto-detected assignment:

| Slot | What it controls | Common auto-detected macros |
|------|------------------|-----------------------------|
| **Load Filament** | Filament load operations | LOAD_FILAMENT, M701 |
| **Unload Filament** | Filament unload operations | UNLOAD_FILAMENT, M702 |
| **Purge** | Nozzle purge/prime | PURGE, PURGE_LINE, LINE_PURGE, PRIME_LINE |
| **Pause** | Print pause | PAUSE, M600 |
| **Resume** | Print resume | RESUME |
| **Cancel** | Print cancel | CANCEL_PRINT |
| **Bed Mesh** | Bed mesh calibration | BED_MESH_CALIBRATE |
| **Bed Level** | Manual bed leveling | BED_SCREWS_ADJUST, SCREWS_TILT_CALCULATE |
| **Clean Nozzle** | Nozzle cleaning | CLEAN_NOZZLE, NOZZLE_CLEAN |
| **Heat Soak** | Chamber heat soak | HEAT_SOAK |
| **Park** | Parking the toolhead (Motion screen, Move tab) | PARK, PARK_TOOLHEAD, TOOLHEAD_PARK |

If your printer doesn't have a matching macro, some slots fall back to HelixScreen helper macros (installed via **Settings > Advanced > Install HelixScreen Macros**). Leave a slot empty to disable that function.

**Load Filament and Unload Filament on a multi-filament printer:** left on **(Auto)**, these two drive your filament system directly rather than running a macro. Choose a macro yourself and it takes over — your macro runs and the filament system's own handling is skipped for that operation, so anything it would have done becomes your macro's job. Set the slot back to **(Auto)** to hand the operation back. The other slots are unaffected. See [Customizing which macro runs](../filament.md#customizing-which-macro-runs).

> **Looking for Load/Unload/Purge button customization?** See the [Filament guide](../filament.md#customizing-which-macro-runs) for a step-by-step walkthrough, including how these buttons interact with AMS systems.

### Per-Material Preheat Macros

You can also assign a custom Klipper macro to each material preset (PLA, PETG, ABS, TPU). This is useful when preheating requires more than just setting temperatures — for example, turning on bed fans for ABS or starting a chamber heater.

Configure per-material macros in **Material Temperatures** (above). Each material can have:

- **Preheat Macro** — A Klipper macro to run when preheating this material
- **Macro Handles Heating** — If enabled, the macro is responsible for setting temperatures. If disabled, HelixScreen sets temperatures first, then runs the macro as an additional step.

These settings apply to the **Preheat widget** on Home or Controls and to the **material preset buttons on the Filament panel**, including the active-spool preset when shown. Long-pressing a Filament preset changes the material assigned to that button; tapping it preheats that material. Individual nozzle, bed, and chamber temperature controls do not run a whole-material macro.

**Macro Handles Heating** is not an enable/disable switch for the macro: the assigned macro runs in either mode. With it enabled, the macro must set every temperature you want. With no macro assigned, or one this printer does not define, the button applies its displayed preset temperatures. Manual preheat does not keep a hotter previous nozzle target for purging; load/unload operations retain their own heating behavior.

---

[Back to Settings](../settings.md) | [Prev: Display & Sound](display-sound.md) | [Next: Hardware & Devices](hardware.md)

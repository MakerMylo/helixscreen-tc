# Filament drying on the heated bed: design

Issue: prestonbrown/helixscreen#1730. Builds on the chamber dryer
(prestonbrown/helixscreen#1299): `TemperatureController::start_chamber_drying`,
`hold_idle_timeout`, `ChamberDryerModal`, `DryerInfo` / `DryingPreset`.

Scope of this document: the **on-bed** placement (spools lying on the build plate).
The under-bed placement is sketched at the end and is not part of the first build.

## What the user gets

On an enclosed printer with a heated bed, **Dry filament** homes the printer while the
bed is empty, lowers the bed away from the nozzle, asks the user to lay the spools on
the plate and close the door, then heats the bed to the preset's drying temperature
(never above 70°C). A chamber appliance with a dryer (stock Panda Breath) runs at the
same time when one is present. From the moment the user confirms the spools are in
until the moment they confirm the spools are out, HelixScreen refuses anything that
could move the toolhead or start a print, and says so in a banner that survives a
restart.

## 1. Where motion and print starts leave HelixScreen

Every app-originated motion or print start goes through five functions:

| Choke point | Carries |
|---|---|
| `src/api/moonraker_api_controls.cpp#MoonrakerAPI::execute_gcode` | every `IMoonrakerAPI::execute_gcode` caller: macros, the console, calibration flows, AMS load/unload, bed mesh, Z offset, probe overlay, PRINT_START preparation |
| `src/api/moonraker_motion_api.cpp#MoonrakerMotionAPI::execute_gcode` | `IMotionAPI` (`home_axes`, `move_axis`, `move_relative`, `move_to_position`, `move_to`): the motion panel's jog pad, hold-to-repeat, keypad and coalescer all end here |
| `src/api/moonraker_job_api.cpp#MoonrakerJobAPI::start_print` / `start_modified_print` | print select, detail view, print start controller, reprint from status |
| `src/api/moonraker_job_api.cpp#MoonrakerJobAPI::resume_print` | resume, including PLR (`src/ui/ui_resume_dispatch.cpp`) |
| `src/api/moonraker_queue_api.cpp#MoonrakerQueueAPI::start_queue` | the job queue |

Both `execute_gcode` bodies already run a gate at exactly this spot:
`src/api/moonraker_gcode_guards.cpp#reject_homing_during_active_print`, which refuses
`G28` (detected by `include/gcode_homing.h#is_homing_gcode`) while a print is active.
The spool latch is a sibling of that function, called beside it in both places.

The only raw `printer.gcode.script` sends outside the API layer are
`src/printer/u1_stock_detection_source.cpp` (detection sensitivity) and
`src/printer/filament_temperature_source.cpp` (temperature). Neither moves anything.

**There is no single choke point, but there is a small closed set: two gcode gates and
three RPC entry points.** The latch gates all five.

### Allowlist, not denylist

A text match for `G28` cannot see a homing buried inside a macro (the AD5X's `_G28`
is the documented case, `include/ams_backend_ad5x_ifs.h`). `PRINT_START`, `CLEAN_NOZZLE`
and most vendor macros home or move. So while the latch is set, the gate refuses every
script unless each line's first token is on a short allowlist:

- heaters: `M104`, `M109`, `M140`, `M190`, `M141`, `M191`, `SET_HEATER_TEMPERATURE`,
  `TURN_OFF_HEATERS`, `SET_TEMPERATURE_FAN_TARGET`
- `SET_IDLE_TIMEOUT`
- fans and lights: `M106`, `M107`, `SET_FAN_SPEED`, `SET_PIN`, `SET_LED`
- the chamber backend's own dryer start and stop tokens, asked from the backend so no
  vendor name enters the gate (`ChamberHeaterBackend::dryer_start_gcode` /
  `dryer_stop_gcode`)
- `M112`, `FIRMWARE_RESTART`, `RESTART`: an emergency stop must always get through,
  and a restart moves nothing
- read-only: `M105`, `M114`, `M115`, `M117`, `RESPOND`

A refusal returns `MoonrakerError::not_ready` with "Spools are on the bed: remove them
and confirm before moving the printer", logs once per episode, and uses the same
`silent` flag the homing guard honours.

### The buttons

`moves_machine="true"` (14 XML files, engine side in
`lib/helix-xml/src/xml/parsers/lv_xml_obj_parser.c`) binds `LV_STATE_DISABLED` to the
`job_holds_machine` subject by name. The census in
`tests/unit/test_job_holds_machine.cpp` keeps every machine-moving control classified.

The latch should not become part of `job_holds_machine`: C++ readers treat that subject
as "a print owns the machine" (the dryer's own start refusal and bed-assist release in
`src/ui/temperature_controller.cpp#end_dry_run` among them), and a drying run is not a
job. Instead:

- a new derived subject `machine_motion_blocked` = `job_holds_machine || spool_latch`,
  published by PrinterState beside `job_holds_machine`
- the engine's `moves_machine` binds `machine_motion_blocked` instead (one literal in
  `lv_xml_obj_parser.c`, which is ours)
- the census and the "installs the guard by construction" test assert against the new
  subject; the 14 files keep their attribute unchanged

This disables XY jogs and extrusion too. With spools on the plate that is the safe
answer, and it keeps one rule.

The gates are the guarantee; the disabled buttons are the courtesy. `ctl` drives the
same API, so it is covered by the gates.

## 2. Can Klipper itself refuse the move?

No clean way exists in stock Klipper at runtime.

- `SET_KINEMATIC_POSITION Z=<position_min>` at the raised height makes Klipper reject
  any lower Z as out of range. It also lies about the position: Mainsail shows a false
  Z, upward moves are then allowed past the physical travel (on a bed-moving printer
  the bed drives into its bottom stop), and the next `G28` erases it. Rejected.
- `SET_STEPPER_ENABLE STEPPER=stepper_z ENABLE=0` refuses nothing and lets a gantry or
  bed drop under gravity. Rejected.
- `[force_move]` enables moves; it refuses none.
- A `gcode_macro` can only `rename_existing` in config, never at runtime.
- Moonraker offers no runtime write of the axis limits.

The one real Klipper-side option is **opt-in, homing only**: a `[gcode_macro G28]` with
`rename_existing` in `assets/config/helix_macros.cfg` (installed by
`include/macro_manager.h`) that errors while a `_HELIX_SPOOL_LATCH` variable is set.
It would cover Mainsail, macros and slicer-started prints that home. It cannot cover
`G1 Z` moves, and it collides with every printer that already wraps `G28`
(`homing_override`, `safe_z_home` shims, the K1/K2 and Qidi vendor macros). Not in the
first build; listed as an open question.

So the start modal says it plainly: HelixScreen blocks its own controls; it cannot stop
Mainsail, a macro run elsewhere, or a print sent from a slicer.

## 3. Capability

### Enclosure (new)

The printer DB (`assets/config/printer_database.json`, read by
`src/printer/printer_detector.cpp`) has no capability block. Per-printer facts are flat
keys read with `printer.value("key", default)`: `toolhead_style`, `probe_type`,
`z_offset_calibration_strategy`. An `"enclosed": true` key follows that shape.

The user override follows `ZMovementStyle` (`include/settings_manager.h`):
`enum class EnclosureStyle { AUTO, ENCLOSED, OPEN }`, applied in PrinterState the way
`src/printer/printer_state.cpp#apply_effective_bed_moves` applies the Z style. AUTO
means: the DB flag, else a configured chamber heater (an appliance heating a chamber
implies a chamber). A chamber sensor alone is not proof: plenty of open printers log
room temperature. The result publishes as `printer_is_enclosed`.

### Bed moves in Z (exists)

`printer_bed_moves` already exists: auto-detected in `printer_state.cpp`
(`corexy` kinematics without QGL), with the `ZMovementStyle` user override. On-bed
drying does not need it. Under-bed drying does.

### Homing direction (under-bed only)

`src/api/moonraker_api_controls.cpp` already parses `configfile.settings.stepper_z`
(`position_max`, and notes `position_endstop`) for the safety limits. Homing is toward
max when `homing_positive_dir` is true, or when `position_endstop` equals
`position_max`. One more field in that parse.

### Gate for the feature

`printer_can_bed_dry` = heated bed && `printer_is_enclosed` && Z travel from
`PrinterState::get_axis_bounds()` of at least the clearance height plus 10 mm.

## 4. The flow

### Entry

One dryer modal, not two. `ChamberDryerModal` grows into the general **Dry filament**
modal:

- the existing chamber dryer row opens it when an appliance dryer exists
- a **Dry filament** action on the bed card of the temperature overlay opens it on
  printers with `printer_can_bed_dry` and no appliance

### Start modal

- preset dropdown (the existing `DryingPreset` list)
- placement: **On the bed** or **Elsewhere in the chamber** (the second is today's
  chamber dryer with bed assist)
- for On the bed:
  - the bed temperature it will use: the preset's temperature capped at 70°C and at
    the bed's own max, shown as a number
  - the warning text: plastic spools soften around 60-70°C, cardboard is fine; the
    printer homes and lowers the bed first; nothing may move until the spools are out;
    Mainsail, macros and slicer prints are not blocked
  - an **I understand** checkbox; **Start** stays disabled until it is ticked
- the chamber appliance switch when an appliance dryer exists
- Start carries `moves_machine="true"` like today's dryer Start

### Sequence

1. **Home if needed**, bed empty. `IMotionAPI::home_axes` when any axis is unhomed.
2. **Clearance move.** Absolute Z to 120 mm, bounded to `axis_maximum.z - 10`. A 1 kg
   spool lying flat is 65-75 mm tall; 120 leaves room for a second layer of small
   spools or a lid. Then park XY at the back of the bed so the plate is reachable.
3. **Place prompt.** "Lay the spools on the bed and close the door." Confirm or Cancel.
   Cancel ends here with nothing heated.
4. **Latch on**, persisted before any heat is sent.
5. **Heat.** Bed to the capped temperature through `TemperatureController::set_target`;
   the appliance dryer when chosen; `hold_idle_timeout` for the run.
6. **Run.** HelixScreen owns the timer when there is no appliance. The persistent
   banner shows remaining time and the latch.
7. **End** (timer, Stop, appliance end, refused start): bed off, idle timeout restored,
   appliance stopped. The latch stays on.
8. **Remove prompt.** "Remove the spools from the bed." Only its confirm clears the
   latch. The banner keeps offering it until then.

### The latch

- Stored in `SettingsManager` (settings.json) as a small record: set time, planned end
  time, bed target, the configured idle timeout to restore. Written in step 4, cleared
  only in step 8.
- `spool_latch` subject published from it; the gates read the subject's backing value
  through PrinterState, the same way `reject_homing_during_active_print` reads print
  state.

### The idle timeout as dead-man switch

`hold_idle_timeout` currently holds for the run plus 30 minutes. For on-bed drying the
margin shrinks to 10 minutes and the hold becomes the backstop: if HelixScreen dies
mid-run, Klipper's own `idle_timeout` fires `TURN_OFF_HEATERS` shortly after the
planned end. Measured on the U1: that gcode does end a heater run
(docs/devel/CHAMBER_HEATER.md). Its `M84` lets a bed-moving printer's bed sink, which
with spools on top of the plate moves them away from the nozzle.

### Interruptions

| Event mid-run | What happens |
|---|---|
| HelixScreen restart | The latch record is read at startup. Before the planned end: the banner returns, the timer resumes from the stored end time, the bed target is re-checked. After it: step 7 cleanup runs, then step 8. The latch stays set throughout. |
| Klipper restart | Klipper drops every heater target and its idle timeout returns to config. The run is over; the next status frame shows bed target 0, which the controller treats as an end (step 7). The latch stays set. |
| Power loss | Klipper comes back unhomed and cold. HelixScreen comes back with the latch set, so its first homing is refused until the spools are confirmed out. This is the case the persisted latch exists for. |
| A print arrives from outside | Not blockable (section 2). The banner is the only defence; the start modal says so. |

## 5. Risks and open questions

1. **Entry point.** Bed card of the temperature overlay, the filament panel, or both?
2. **Clearance.** 120 mm absolute, or spool height plus margin chosen in the modal?
3. **Allowlist strictness.** Strict (above) refuses user macros during the run, even
   harmless ones. Accept that?
4. **Open printers.** Hide the feature, or allow with an extra warning? A bed-slinger
   under a box is "enclosed" only through the override.
5. **Klipper-side G28 guard** in `helix_macros.cfg`: worth offering opt-in later,
   given the collision with vendor `G28` wrappers?
6. **The X1C reference.** Its actual sequence (spool on the plate? bed temperature per
   material? lid?) should be checked before the modal text is final.
7. **Dead-man margin.** 10 minutes past the planned end, or tighter?

## Under-bed placement (later)

Needs `printer_bed_moves`, the homing direction above, and the opposite guard: Z-up
moves (bed down) and homing toward max are the hazards. The idle timeout's `M84` is a
hazard here, not a backstop: a sinking bed lands on the spools, so this placement
cannot rely on the dead-man switch and needs its own answer before it ships.

# Tools Panel (tool changers)

On a printer with a physical tool changer (klipper-toolchanger's `toolchanger` object) a
Tools button appears in the navigation bar. The panel shows one column per tool, in the
shape of a multi-material printer's filament display:

- **Tool name** at the top. Tap it to pick that tool up; the column of the tool on the
  carriage is outlined.
- **Filament path**: the reverse bowden coming in, the tool's sensors (when it has any),
  the toolhead in your configured toolhead style, and the nozzle. Each part is drawn in the
  slot's filament colour where filament is known to be, and as an empty tube otherwise.
- **Material** and the extruder's **temperature** (`182 / 250°` while it heats).
- **Options** opens the tool's actions.

## Where "filament is here" comes from

For each part of the path the panel uses the best source it has:

1. A **sensor** for that part of the path, when the printer has one. A
   `filament_switch_sensor` belongs to a tool when its name carries the tool number and
   the end of the path it watches: `T0_entry`, `T0_toolhead`, `t3_pre`, `tool2_nozzle`,
   `fd_ex1`, `extruder1_runout` all work. A sensor with the **Entry** role (Settings >
   Devices > Sensors) counts as an entry sensor; anything else counts as the toolhead
   end. An explicit `lane` in the sensor's settings overrides the name.
2. Otherwise the **printer's own memory**: the `tc_loaded` variable in Klipper's
   `[save_variables]`, which the `LOAD_TOOL` / `UNLOAD_TOOL` macros maintain. `TOOL_LOADED
   TOOL=2 STATE=1` sets it by hand.
3. Otherwise the **slot's status** in the filament system.

## The actions overlay

The tool's name is the title. Below it:

- **Changes**: the pickup and drop-off record the printer's macros keep in
  `save_variables` (`tc_stats`): pickups (PU), drop-offs (PD), failed pickups (FPU),
  failed drop-offs (FPD), pickups recovered by a retry (REC) and the success rate.
- The same **path drawn sideways**.
- **Pick up / Dock**, **Load**, **Unload**, **Extrude**, **Retract**. Pick up runs the
  tool change; Dock sends `UNSELECT_TOOL`. The others run the printer's `LOAD_TOOL`,
  `UNLOAD_TOOL`, `TOOL_EXTRUDE` and `TOOL_RETRACT` macros with `TOOL=<n>`, so a docked
  tool can be loaded or purged where it sits. All five are refused while a print holds
  the machine.
- **Filament**: the slot's material and colour, with **Change** opening the shared slot
  editor.

## Klipper side

The panel needs nothing beyond klipper-toolchanger to draw the tools. For the record and
the memory it reads `save_variables`, so add `[save_variables]` to printer.cfg if you
have not, and have your load/unload macros write `tc_loaded` and your tool change hooks
write `tc_stats`; the variable names and shapes are documented in
`include/toolchanger_vars.h`.

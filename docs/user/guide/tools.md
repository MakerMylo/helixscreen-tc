# Tools Panel (tool changers)

On a printer with a physical tool changer (klipper-toolchanger's `toolchanger` object) a
Tools button appears in the navigation bar. The panel shows one column per tool, in the
shape of a multi-material printer's filament display:

- A **colour bar** in the toolhead's own colour (see Settings below).
- **Tool name** below it. Tap it to pick that tool up; the column of the tool on the
  carriage is outlined.
- **Filament path**: the reverse bowden coming in, the tool's sensors (when it has any),
  the toolhead in your configured toolhead style and its own colour, and the nozzle. The
  tube and the extrudate below the nozzle are drawn in the slot's filament colour where
  filament is known to be, and as an empty tube otherwise.
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

The tool's name is the title. Below it, three columns:

- **Left**: the **change success rate** as one number, green above 95% and amber at or
  below it, with the clean pickups and drop-offs out of those attempted underneath
  (`Pick up 10/10 · Drop off 9/10`). The record is what the printer's macros keep in
  `save_variables` (`tc_stats`). Below that, the **filament form**: the material (a
  dropdown), the colour (tap the block to pick one) and whether the printer's memory says
  the tool is loaded. **Save** is enabled once something differs from what the slot holds
  and writes the slot through the filament system, as the slot editor would.
- **Middle**: the same **path drawn sideways**, and the last action's result under it.
- **Right**: **Pick up / Dock**, **Load**, **Unload**, **Extrude**, **Retract**. Pick up
  runs the tool change; Dock sends `UNSELECT_TOOL`. The others run the printer's
  `LOAD_TOOL`, `UNLOAD_TOOL`, `TOOL_EXTRUDE` and `TOOL_RETRACT` macros with `TOOL=<n>`,
  so a docked tool can be loaded or purged where it sits. All five are refused while a
  print holds the machine.

## Settings

Settings > Devices > **Tool Changer** (shown on a tool changer):

- **Number of tools**: *Auto* shows the tools Klipper reports. A number shows that many
  columns instead; columns past the reported tools are drawn as absent.
- **Toolhead colours**: one row per tool, defaulting to red, orange, yellow, green, blue
  and purple for T0 to T5 (then further distinct hues). Tap a row to pick a colour,
  **Reset** to go back to the default. The colour is the physical toolhead's; the
  filament always keeps the slot's colour.

Both are stored per printer in `helixconfig.json` (`toolchanger/tool_count`,
`toolchanger/tool_colors`) and apply at once.

## Klipper side

The panel needs nothing beyond klipper-toolchanger to draw the tools. For the record and
the memory it reads `save_variables`, so add `[save_variables]` to printer.cfg if you
have not, and have your load/unload macros write `tc_loaded` and your tool change hooks
write `tc_stats`; the variable names and shapes are documented in
`include/toolchanger_vars.h`.

# Printer image live callouts: in-app tagger (phase 3)

prestonbrown/helixscreen#1397. Phases 1 and 2 (chips, modes, leader lines, bed glow) have
shipped; their design lives in `docs/devel/PANEL_WIDGET_GUIDE.md` §
"The printer image callouts instance". This file keeps only what still guides phase 3, a
stretch goal.

## Goal

Let a user tag their printer image on the device. It is the only way a custom photo gets chips
pinned to its parts instead of docked along the picture's edge.

## Behaviour

- Entry point: Settings → Printer → Tag printer image, opening a full-screen overlay.
- The same six prompts as `tools/printer-regions-tagger.html`, one tap each: nozzle tip, part
  fan, bed near-left, bed near-right, an empty spot inside the enclosure, the light. Part fan,
  chamber and light can be skipped. Skip and Undo buttons.
- A review step renders live chips at the tapped points before saving.

## Storage and reset

- Saved to `<config>/printer_image_regions.json`, in the `regions.json` entry format
  (`assets/images/printers/README.md`: points normalized 0..1 over the source image, plus its
  `size`), so `parse_image_regions()` reads both files.
- A saved entry overrides the shipped entry for that image only.
- Reset returns the image to its shipped points.

## Open questions

- The key a custom photo is stored under (shipped images use `printer_image_basename()`, which
  returns empty for anything outside the shipped printers directory).
- Whether a user entry whose `size` no longer matches its image is ignored, as the shipped
  `[regions]` guard treats a re-cropped PNG.

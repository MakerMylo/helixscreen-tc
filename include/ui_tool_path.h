// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvgl.h>

/**
 * @file ui_tool_path.h
 * @brief `<tool_path>` - one tool changer tool's filament path, drawn live.
 *
 * A single tool's path: the reverse bowden coming in, up to two sensor pips
 * (entry, toolhead), the toolhead glyph in the user's configured style, and
 * the short run down to the nozzle. Each segment is painted in the slot's
 * filament colour when filament is known to be there, and as an empty PTFE
 * tube otherwise.
 *
 * Where "known" comes from, per segment:
 *   - a sensor for that part of the path, when the printer has one
 *     (helix::tool_sensors resolves them by name);
 *   - otherwise the macros' memory (helix::ToolchangerVars::loaded_for);
 *   - otherwise the AMS slot's own status (a loaded slot is drawn loaded).
 *
 * XML attributes:
 *   tool_index="3"          the tool this instance draws (0-based)
 *   tool_subject="name"     follow an int subject for the index instead
 *   mode="vertical|horizontal|swatch"
 *       vertical   (default) bowden above, nozzle below - the panel column
 *       horizontal bowden from the left - the actions overlay
 *       swatch     just the filament colour as a rounded block
 *
 * The widget observes the slot colour and status, the tool set, the saved
 * variables and the sensor readings, and repaints itself; nothing else needs
 * to poke it. Drawing in C++ is the structural exception the declarative-UI
 * rules allow (a draw hook) - the widget has no children and sets no styles
 * on anything but itself.
 */

namespace helix::ui {

/// Register `<tool_path>` with the XML engine. Idempotent.
void ui_tool_path_register();

/// Change the tool an instance draws (the actions overlay re-points one
/// widget as the user opens it for different tools).
void ui_tool_path_set_tool(lv_obj_t* obj, int tool_index);

} // namespace helix::ui

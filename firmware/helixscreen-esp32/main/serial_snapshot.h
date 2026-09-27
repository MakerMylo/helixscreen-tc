// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Screenshots over the serial console: send "snap" and the active screen comes
// back as raw-deflated RGB565, base64 on "SNAP:" lines between HELIX-SNAP
// markers. scripts/esp32_serial_snapshot.py drives it and writes a PNG.

#ifdef __cplusplus
extern "C" {
#endif

/// Start the console reader. Call once, after the console is up.
void serial_snapshot_start(void);

/// Take and stream a requested snapshot. Call from the LVGL thread only.
void serial_snapshot_poll(void);

#ifdef __cplusplus
}
#endif

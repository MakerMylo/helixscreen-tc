# ESP32 image size: exceptions off

The ESP32 app image (`firmware/helixscreen-esp32/size_budget.json`: the 6.75MB OTA slot
minus a 200KiB margin, 6,873,088 bytes) is at 6,676,048 bytes with std::locale out of the
link. Firmware-compiled code uses `text_io.h`, `helix_regex.h` and `helix_fs.h` in place of
iostreams, `<regex>` and `<filesystem>`; `scripts/check_esp32_app_srcs.py` rejects those
includes and `scripts/check_esp32_size.py` fails the esp32-build job if libstdc++'s
`locale_init.o` is linked again, naming the object that pulled it.

Measure with the CI toolchain (`espressif/idf:v5.5.5`): `idf.py size-files` diffed between
two builds, and the map's "Archive member included" section for why a libstdc++ member is
linked.

## Exceptions off on ESP32 (~470K+)

`.eh_frame` is ~468K, plus LSDA tables and landing pads. The work is making ESP32 paths
non-throwing in shared code, then `CONFIG_COMPILER_CXX_EXCEPTIONS=n` and
`JSON_NOEXCEPTION`:

- ~1,100 JSON accesses (744 `.get<T>()`, 319 `.value()`, 38 `.at()`, 35 `json::parse`)
  throw on a type mismatch today and are caught. With exceptions off they abort, so a
  `null` from Moonraker would reboot the device. They move to a non-throwing accessor
  layer; `json::parse` to `parse(s, nullptr, false)` + `is_discarded()`.
- ~249 `try` blocks in ~72 files; 132 `std::sto*` (text_io's `parse_int`/`parse_double`
  replace them); 22 throwing `std::any_cast`. Filesystem and regex calls already do not
  throw.
- Catch-all safety nets (`ui_update_queue.h`, `ui_event_safety.h`,
  `ui_event_trampoline.h`) stay on desktop behind `__cpp_exceptions`.

To be scoped in detail before work starts.

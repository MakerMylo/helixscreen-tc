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

## Exceptions off on ESP32 (~500K)

With `CONFIG_COMPILER_CXX_EXCEPTIONS=n`, IDF's `ld.common` stops linking `.eh_frame` at all
(440K on the current image, libstdc++'s included), and `-fno-exceptions` drops the LSDA
tables and landing pads on top. Desktop keeps exceptions; nothing below changes a desktop
code path except where it says so.

With exceptions off, every throw is an abort: nlohmann's `JSON_THROW` becomes
`std::abort()` (it keys on `__cpp_exceptions`), and libstdc++'s `__throw_*` reach IDF's
aborting `__cxa_throw`. So the work is making every throw a firmware-compiled path can
reach impossible, then flipping the switch. A missed site is a reboot, not a log line.

Scope (firmware-compiled sources and the headers they include): 221 files, 332 `try`,
~1,155 JSON reads (`.get<>`, `.value()`, `.at()`, `json::parse`), 175 `std::sto*` /
`std::any_cast`, 75 `throw`. The exceptions-off probe build fails 343 translation units,
all in our code: no third-party header (spdlog, fmt, LVGL, nlohmann) needs a change.
Implicit JSON conversions (`std::string s = j["k"]`) occur only in `include/config.h`
(checked with `-DJSON_USE_IMPLICIT_CONVERSIONS=0` over the whole list).

### Recipe

Non-throwing building blocks, all in place:

| Throwing form | Replacement |
|---|---|
| `j.value("k", D)` on a json object | `json_util::safe_<T>(j, "k", D)`, T from D's type |
| `v.get<T>()` with no type guard on `v` | `json_util::as_<T>(v, D)` |
| `j.at("k")`, `j["k"]` on a json that may not be an object | `j.find("k")` / `json_util::detail::find(j, "k")` plus a type check |
| `json::parse(s)` | `json::parse(s, nullptr, false)`, then `is_discarded()` |
| `j.dump()` of text nothing validated | `json_util::safe_dump(j)` |
| `std::stoi/stol/stoll/stoul/stoull/stof/stod(s[, nullptr, base])` | `text_io::parse_leading<int/long/long long/unsigned long/unsigned long long/float/double>(s[, base])` |
| `std::any_cast<T>(a)` | `const T* p = std::any_cast<T>(&a)` plus a null check |

A `.get<T>()` whose value was type-checked in the same condition or by an early return
just above (`is_string()`, `is_number()`, `is_boolean()`, ...) cannot throw and stays.

Converting a `try` block: find each call in the body that could throw, replace it per the
table, and put the catch arm's behaviour (its log line, `return`, `continue`, fallback
value) on that call's failure branch. Then delete the `try`/`catch`. Where a catch arm
aborted the whole operation, the converted code aborts it too, not just the one field.
A body that can still throw after that (a `throw` of our own, `std::thread`, a library
call not in the table) is reported, not guessed at.

The catch-all safety nets (`ui_update_queue.h`, `ui_event_safety.h`) keep their `try` on
desktop behind `#if defined(__cpp_exceptions)`.

### Order

1. Readers and parsers (`json_util::as_*`, `text_io::parse_leading`), catch-all nets,
   `config.h`/`config.cpp`.
2. Conversion batches by subsystem, each reviewed and merged to main on its own.
   Desktop keeps exceptions, so every batch ships alone.
3. `CONFIG_COMPILER_CXX_EXCEPTIONS=n` in `sdkconfig.defaults`, the size gate re-measured,
   and the ban kept by the compiler itself: a `try` in a firmware file stops compiling.

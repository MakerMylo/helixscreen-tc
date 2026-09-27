# ESP32 image size: drop std::locale, then exceptions

The ESP32 app image outgrew its budget: 6,950,128 bytes on main d0144d86d against
6,873,088 (`firmware/helixscreen-esp32/size_budget.json`, the 6.75MB OTA slot minus a
200KiB margin). It grew 113K in one day of ordinary Core+AMS feature work, so trimming
objects buys hours. Two structural levers remain, measured with the CI toolchain
(`espressif/idf:v5.5.5`, `idf.py size-files` diffed between builds, `--cref` in the map).

Branch `chore/esp32-size-budget`, worktree `.worktrees/esp32-size-budget`.

## Phase 1: no std::locale in the image (~220K)

libstdc++ builds every standard facet (char and wchar_t, cxx11 and COW ABI) the first
time anything touches `std::locale`: 184K of `locale-inst.o`, `wlocale-inst.o`,
`cxx11-*locale-inst.o`, shim facets, stream instantiations. It drops out only when
nothing references `std::locale`. Three things do today:

| Source | Scope | Removal |
|---|---|---|
| fmt locale support (`format_facet<std::locale>`, locale-aware `write_float`) | every object that formats a float | `FMT_USE_LOCALE=0` for the ESP32 build. Measured alone: -11.8K. No `{:L}` specs exist. |
| iostreams | 64 ESP32 source files: 61 `ifstream`, 56 `getline`, 40 `istringstream`, 34 `ostringstream`, 20 `ofstream` | `include/text_io.h` helpers (below), `fmt::format`, string ops |
| `std::regex` | 12 files, ~128 sites, plus data-driven patterns | `helix::Regex`, an in-tree engine used on every platform (below). libstdc++'s regex scanner takes a `std::locale` internally, so custom traits cannot avoid it. std::regex template code itself is another ~28K. |

### text_io helpers

One header, `include/text_io.h` (implementation in `src/system/text_io.cpp`), so the 64
files do not each hand-roll a replacement:

- `std::optional<std::string> read_file(const std::string& path)` - whole file, binary-safe
- `bool write_file(const std::string& path, std::string_view data)`
- `bool write_file_atomic(const std::string& path, std::string_view data)` - tmp + rename,
  the shape 19 call sites hand-roll today; each migrated site keeps its own semantics
- `for_each_line(std::string_view text, F fn, char delim = '\n')` - strips a trailing `\r`
- `split_ws(std::string_view)` for `istringstream >> token` loops
- `parse_int` / `parse_double` returning `std::optional`, via `std::from_chars`

Desktop builds use the same code: no ESP32-only forks.

### helix::Regex

A small backtracking engine covering exactly the syntax the tree uses: literals and
escapes, `.`, classes with ranges and negation, `\d \D \s \S \w \W`, groups (capturing
and `(?:)`), alternation, `* + ? {n} {n,} {n,m}` greedy and lazy, `^ $`, icase. API
shaped after the calls in use: `search`, `match` (full), `replace`, capture access.
Patterns outside the subset fail to compile with an error string, as `std::regex_error`
does today.

Proof it is a drop-in: a differential test runs every pattern literal in `src/`, every
pattern in `assets/config/print_start_profiles/*.json`, and a corpus of real inputs
(Klipper responses, gcode headers, AD5X IFS lines) through both engines on desktop and
requires identical match results and captures. Catastrophic backtracking is bounded by a
step limit that reports no-match.

### Gates

- Lint: a file in `app_srcs.txt` may not include `<sstream> <iostream> <fstream> <regex>`
  (quality-checks, seconds). Catches a regression before the 30-minute firmware build.
- esp32-build: fail if `locale_init.o` is in the linked image (map), with the referencing
  objects from `--cref` in the message.

### Order

1. `FMT_USE_LOCALE=0` define.
2. `text_io.h` + unit tests.
3. `helix::Regex` + differential test (parallel with 4).
4. Streams out of the non-regex files, in disjoint batches.
5. The 12 regex files move to `helix::Regex`; streams out of those too.
6. Gates on, measure, budget green.

## Phase 2: exceptions off on ESP32 (~470K+)

`.eh_frame` is 468,620 bytes, plus LSDA tables and landing pads. The work is making ESP32
paths non-throwing in shared code, then `CONFIG_COMPILER_CXX_EXCEPTIONS=n` and
`JSON_NOEXCEPTION`:

- ~1,100 JSON accesses (744 `.get<T>()`, 319 `.value()`, 38 `.at()`, 35 `json::parse`)
  throw on a type mismatch today and are caught. With exceptions off they abort, so a
  `null` from Moonraker would reboot the device. They move to a non-throwing accessor
  layer; `json::parse` to `parse(s, nullptr, false)` + `is_discarded()`.
- 249 `try` blocks in 72 files; 132 `std::sto*`; 22 throwing `std::any_cast`;
  filesystem calls to their `error_code` overloads.
- Catch-all safety nets (`ui_update_queue.h`, `ui_event_safety.h`,
  `ui_event_trampoline.h`) stay on desktop behind `__cpp_exceptions`.

Scoped in detail after Phase 1 lands.

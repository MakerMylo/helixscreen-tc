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
nothing references `std::locale`. Four things do:

| Source | Scope | Removal |
|---|---|---|
| fmt locale support (`format_facet<std::locale>`, locale-aware `write_float`) | every object that formats a float | `FMT_USE_LOCALE=0` for the ESP32 build. Measured alone: -11.8K. No `{:L}` specs exist. |
| iostreams | 64 ESP32 source files: 61 `ifstream`, 56 `getline`, 40 `istringstream`, 34 `ostringstream`, 20 `ofstream` | `include/text_io.h` helpers (below), `fmt::format`, string ops |
| `std::regex` | 12 files, ~128 sites, plus data-driven patterns | `helix::Regex`, an in-tree engine used on every platform (below). libstdc++'s regex scanner takes a `std::locale` internally, so custom traits cannot avoid it. std::regex template code itself is another ~28K. |
| `std::filesystem` | 30 ESP32 source files, 6 headers | `helix::fs` (below). libstdc++'s `fs_path.o` converts paths through a wide `codecvt` and wstring streams, `fs_ops.o` copies through a `filebuf` and `fs_dir.o` follows: with every app object off streams and regex, these three still link the whole 150K locale stack (traced through the map's archive-member chain). |

### text_io helpers

`include/text_io.h` (namespace `helix::text_io`, implementation `src/system/text_io.cpp`,
tests `[text_io]`). Desktop builds use the same code: no ESP32-only forks.

| API | Contract |
|---|---|
| `read_file(path)` -> `optional<string>` | whole file, byte for byte; nullopt only if it cannot be opened or a read fails |
| `read_first_line(path)` -> `optional<string>` | one `std::getline`: `\n` removed, `\r` kept; `""` for an empty file |
| `file_size(path)` -> `optional<uint64_t>` | `stat()` size |
| `write_file(path, data)` -> `bool` | create/truncate; errno from the failing call |
| `write_file_atomic(path, data, Durability::None\|Fsync)` -> `bool` | `path + ".tmp"`, close, `rename`; on failure tmp removed, target untouched, errno kept. `Fsync` also fsyncs the tmp and the directory (config's contract). Does not resolve symlinks: call `helix::paths::write_target()` first where the old code did |
| `File` = `unique_ptr<FILE>`; `open_file(path, "rb"\|"wb"\|"ab")` | empty on failure, errno set |
| `write_all(FILE*, sv)` -> `bool` | false on a short write |
| `close(File&)` -> `bool` | flush + fclose; what `ofstream::good()` after the writes would say |
| `LineReader r(path, delim='\n')`; `if (!r)`; `r.next(line)`; `r.last_had_delimiter()` | `std::getline` on a file, record by record |
| `lines(text, delim='\n')` | range of `string_view`, identical to a `while (getline(istringstream))` loop; `break`/`continue` work |
| `split_ws(sv)` -> `vector<string_view>` | the tokens successive `>> token` reads give |
| `parse_int<T=long long>(sv, base=10)`, `parse_double(sv)` -> `optional` | whole view must be the number; one leading `+` allowed; no whitespace; `.` always; no `0x` |

Includes: `#include "text_io.h"` and `namespace tio = helix::text_io;` at file scope (or
spell `helix::text_io::`). Remove `<sstream> <fstream> <iostream> <iomanip> <istream>
<ostream>` once the file has no stream left. Add `#include <fmt/format.h>` only if the file
does not already get fmt through spdlog.

#### Conversion recipe

Rules that apply to every row: keep the old variable names; where the old loop body used
`line` as a `std::string` (mutated it, passed it to something taking `std::string&` or
`const std::string&`), write `std::string line(sv);` as the loop's first statement and
leave the body untouched. Error paths stay the error paths: every `if (!file)` /
`is_open()` check maps to the `nullopt` / `false` / empty-`File` check shown.

| Old | New |
|---|---|
| `std::ifstream f(p); if (!f) {X} std::stringstream b; b << f.rdbuf(); s = b.str();` (also `ostringstream`, `istreambuf_iterator` pairs) | `auto s = tio::read_file(p); if (!s) {X}` then use `*s` |
| `std::ifstream f(p); ... json::parse(f)` / `f >> j` | `auto text = tio::read_file(p); if (!text) {X}` then `json::parse(*text)`. With no open check before it: `json::parse(tio::read_file(p).value_or(""))` (an empty input throws `parse_error` as the empty stream did) |
| `json::parse(std::ifstream(p))` | `json::parse(tio::read_file(p).value_or(""))` |
| `std::ifstream f(p); if (!f) {X} std::string line; std::getline(f, line);` (sysfs one-liners) | `auto line = tio::read_first_line(p); if (!line) {X}` then use `*line` |
| `std::ifstream f(p); if (!f) {X} std::string line; while (std::getline(f, line)) {B}` (also with `&& lines_read < max`) | `tio::LineReader f(p); if (!f) {X} std::string line; while (f.next(line)) {B}` (same extra conditions) |
| `std::getline(f, line); // skip header` on a file | `f.next(line);` |
| `std::getline(cmd, arg, '\0')` on a file | `tio::LineReader cmd(p, '\0');` then `cmd.next(arg)` |
| `std::istringstream s(text); std::string line; while (std::getline(s, line[, d])) {B}` | `for (std::string_view sv : tio::lines(text[, d])) { std::string line(sv); B }` (skip the copy when `B` only reads `line` through `string_view`-friendly calls) |
| `std::istringstream s(text); std::string tok; while (s >> tok) {B}` | `for (std::string_view sv : tio::split_ws(text)) { std::string tok(sv); B }` |
| `std::istringstream s(text); s >> a >> b >> c;` then `if (s)` | `auto t = tio::split_ws(text);` then `t.size() >= 3` and `tio::parse_double(t[0])` etc.; all must succeed where the old code required the stream to be good |
| `std::istringstream(text) >> x;` (one number) | `auto t = tio::split_ws(text); if (!t.empty()) if (auto v = tio::parse_double(t[0])) x = *v;` (`parse_int<T>` for integers) |
| `std::stoull(tok, nullptr, 16)` in a try/catch | `if (auto v = tio::parse_int<unsigned long long>(tok, 16)) ... else <the catch body>` |
| `std::ofstream o(p); if (!o) {X} o << data; if (!o) {Y}` (whole content known) | `if (!tio::write_file(p, data)) {X}` (merge `X`/`Y`; errno is set) |
| hand-rolled `path.tmp` + write + `rename` (+ `remove(tmp)` on failure) | `if (!tio::write_file_atomic(target, data)) { log strerror(errno); ... }`. A site that also fsyncs uses `Durability::Fsync` |
| `std::ofstream o(p); o << a << b;` streamed pieces, or a file written line by line | `auto o = tio::open_file(p, "wb"); if (!o) {X}` then `tio::write_all(o.get(), piece)` per piece, and `if (!tio::close(o)) {Y}` where the old code checked `good()` |
| `std::ofstream probe(p[, ios::app]); return (bool)probe;` (write probes) | `return static_cast<bool>(tio::open_file(p, "wb"` or `"ab"));` |
| `std::ifstream f(p, ios::binary\|ios::ate); if (!f) {X} auto n = f.tellg();` (readable + size probe) | `if (!tio::open_file(p, "rb")) {X} auto n = tio::file_size(p).value_or(0);` |
| `seekg` / `read` / `tellg` random access on a binary file | `auto f = tio::open_file(p, "rb");` then `std::fseek(f.get(), off, SEEK_SET)`, `std::fread(buf, 1, n, f.get()) == n`, `std::ftell`. (`long` offsets: 2GB ceiling on 32-bit targets) |
| `std::ostringstream os; os << "X=" << i << " Y=" << s; return os.str();` | `return fmt::format("X={} Y={}", i, s);` or build a `std::string` with `+=`; `os.str()` becomes the string itself |
| `os << double_or_float` with no manipulators | `fmt::format("{:g}", v)`: iostreams print `%g` with precision 6, and plain `{}` prints the shortest round-trip form, which changes G-code text |
| `os << std::fixed << std::setprecision(N) << v` | `fmt::format("{:.Nf}", v)` (N literal) |
| `os << bool_value` | `fmt::format("{:d}", b)` (streams print 1/0, `{}` prints true/false) |
| `os << int8_t/uint8_t/char` | streams print these as characters: `fmt::format("{}", static_cast<char>(c))`; a numeric intent needs `static_cast<int>` |
| `os << std::endl` | `'\n'` |

### helix::fs

`include/helix_fs.h`, POSIX-backed, never throws, paths are `std::string`. Sizes stay
in `text_io::file_size`.

| API | Contract |
|---|---|
| `join_path(a, b)` | `path(a) / b` |
| `filename`, `parent_path`, `stem`, `extension` -> `string_view` | `std::filesystem::path` semantics, differentially tested |
| `lexically_normal(p)` -> `string` | same |
| `exists`, `is_directory`, `is_regular_file` | follow symlinks; any stat failure is `false` (the ESP32 VFS reports a missing path as `ENODATA`) |
| `is_symlink(p)` | lstat; always `false` on ESP32 |
| `is_owner_executable(p)` | `st_mode & S_IXUSR` |
| `mtime_ns(p)` -> `optional<int64_t>` | nanoseconds since the Unix epoch |
| `space_available(p)` -> `optional<uint64_t>` | statvfs `f_bavail * f_frsize`; `nullopt` on ESP32 (no statvfs) |
| `canonical(p)` -> `optional<string>` | `realpath`; on ESP32 only normalizes (no symlinks, no existence check) |
| `create_directories(p)` -> `bool` | true when `p` is a directory afterwards, **including when it already was** |
| `remove(p)` -> `bool` | file or empty dir; true only when removed; false + `errno == ENOENT` when absent |
| `rename(from, to)` -> `bool` | replaces an existing `to` |
| `copy_file(from, to, overwrite = false)` -> `bool` | `false` + `EEXIST` when `to` exists and `!overwrite`; a failed copy removes the partial `to` |
| `list_dir(dir)` -> `optional<vector<DirEntry>>` | `DirEntry{name, path, is_dir, is_regular}`, no `.`/`..`; `nullopt` + errno when `dir` cannot be opened; partial on a read error |

Every mutation leaves errno set on failure. `ec.message()` becomes `std::strerror(errno)`
(add `<cerrno>` and `<cstring>`), and `e.what()` from a caught `filesystem_error`
becomes `fmt::format("{}: {}", path, std::strerror(errno))`.

#### Conversion recipe

`namespace hfs = helix::fs;` at file scope when used more than twice. Delete `namespace fs
= std::filesystem;`, `#include <filesystem>`, and any `std::experimental::filesystem`
fallback block once unused. A variable of type `fs::path` becomes `std::string`.

| Old | New |
|---|---|
| `fs::path(a) / b` (also `p / "x" / "y"`) | `hfs::join_path(a, b)` (nest: `hfs::join_path(hfs::join_path(p, "x"), "y")`) |
| `fs::path(p).parent_path()`, `.filename()`, `.stem()`, `.extension()`, `.lexically_normal()` | `hfs::parent_path(p)` etc. They return `string_view`; wrap in `std::string(...)` where a `std::string` is stored or `+`-concatenated |
| `.string()`, `.native()`, `.c_str()` on what is now a `std::string` | drop `.string()`/`.native()`; `.c_str()` stays |
| `entry.path().extension() == ".bin"` | `hfs::extension(e.path) == ".bin"` |
| `fs::exists(p)`, `fs::exists(p, ec)`, `fs::exists(p, ec) && !ec` | `hfs::exists(p)` (same for `is_directory`, `is_regular_file`, `is_symlink`) |
| `fs::remove(p, ec);` (result ignored) / `fs::remove(p);` in a try | `hfs::remove(p);` |
| `if (!fs::remove(p, ec) && ec) { log(ec.message()) }` | `if (!hfs::remove(p) && errno != ENOENT) { log(std::strerror(errno)) }` |
| `bool removed = fs::remove(p, ec);` / `if (fs::remove(p, ec))` | `bool removed = hfs::remove(p);` (true only when something was removed, as before) |
| `fs::create_directories(p, ec); if (ec) {X}` | `if (!hfs::create_directories(p)) {X}` |
| `fs::create_directories(p); ... if (fs::is_directory(p, ec))` | `if (hfs::create_directories(p))` - true already covers "is a directory now" |
| `fs::rename(a, b, ec); if (ec) {X}` | `if (!hfs::rename(a, b)) {X}` |
| `fs::copy_file(a, b)` (in a try) | `hfs::copy_file(a, b)` checked for `false` in place of the catch |
| `fs::copy_file(a, b, fs::copy_options::overwrite_existing[, ec])` | `hfs::copy_file(a, b, /*overwrite=*/true)` |
| `auto n = fs::file_size(p, ec); if (ec) {X}` | `auto n = helix::text_io::file_size(p); if (!n) {X}` then `*n` |
| `fs::file_size(p, ec) == 0 \|\| ec` | `helix::text_io::file_size(p).value_or(0) == 0` |
| `fs::space(p).available` in a try | `hfs::space_available(p)`; the catch body becomes the `nullopt` branch |
| `fs::canonical(p[, ec])` | `hfs::canonical(p)` -> `optional<string>`; the catch/ec body becomes the `nullopt` branch |
| `fs::status(p).permissions() & fs::perms::owner_exec` | `hfs::is_owner_executable(p)` |
| `fs::last_write_time(p[, ec])` compared or sorted | `hfs::mtime_ns(p)` (`nullopt` where `ec` was set or the call threw) |
| `ftime - file_time_type::clock::now() + system_clock::now()` then `to_time_t` | `static_cast<time_t>(*hfs::mtime_ns(p) / 1'000'000'000)` |
| `for (const auto& entry : fs::directory_iterator(d)) {B}` inside a `try { } catch (filesystem_error)` | `auto entries = hfs::list_dir(d); if (!entries) {catch body}` then `for (const auto& e : *entries) {B}` |
| `for (const auto& entry : fs::directory_iterator(d, ec)) {B}` (no check after) | `if (auto entries = hfs::list_dir(d)) for (const auto& e : *entries) {B}` |
| `directory_iterator it(d, ec); if (ec) {X} for (; it != end; it.increment(ec)) { if (ec) {Y; break;} B }` | `auto entries = hfs::list_dir(d); if (!entries) {X}` then loop `B` over `*entries`; `Y` (read error) has no separate signal: drop it |
| `entry.path()` | `e.path` (a `std::string`) |
| `entry.path().filename().string()` | `e.name` |
| `fs::is_regular_file(entry.path())`, `entry.is_regular_file()` | `e.is_regular` |
| `fs::is_directory(entry.path())`, `entry.is_directory()` | `e.is_dir` |
| `try { <only fs calls> } catch (const fs::filesystem_error& e) { log(e.what()) }` | drop the try; check each call's `bool`/`optional` and log `std::strerror(errno)` in the failing branch. Keep a try only when the block also calls something else that throws (JSON) |


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

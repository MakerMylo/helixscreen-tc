#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail if the ESP32 app image exceeds its budget, or if the link pulled in
libstdc++'s std::locale machinery or any exception-handling support. Usage:
   check_esp32_size.py build/helixscreen_esp32.bin firmware/helixscreen-esp32/size_budget.json \\
       [build/helixscreen_esp32.map]"""
import json
import os
import re
import sys

# locale_init.o builds every standard facet (char and wchar_t, both string ABIs),
# ~150K, the first time anything touches std::locale: iostreams, <regex>,
# std::filesystem. app_srcs.txt files are linted for those includes; this catches
# the ones that arrive through a header or a library.
LOCALE_MEMBER = "locale_init.o"

# The image is built without exceptions (sdkconfig.defaults). Re-enabling them
# costs ~1.3MB and still fits the budget, so the size check alone would not
# notice: the personality routine is linked only for code compiled with
# exceptions, and IDF emits an .eh_frame output section only when they are on.
EH_MEMBER = "eh_personality.o"
EH_FRAME_SECTION = re.compile(r"^\.eh_frame\s+0x[0-9a-f]+\s+0x([0-9a-f]+)", re.M)


def inclusion_chain(map_text: str, member: str) -> list[str]:
    """Why the linker pulled `member`, from the map's archive-member section."""
    head = map_text.split("Discarded input sections", 1)[0].splitlines()
    why = {}
    for i, line in enumerate(head):
        # ld writes the referencer on the member's own line when the member name
        # is short, and on the next line otherwise.
        m = re.match(r"^(\S+\.a\(([^)]+)\))(\s+.*)?$", line)
        if not m:
            continue
        rest = m.group(3) if m.group(3) and m.group(3).strip() else \
            (head[i + 1] if i + 1 < len(head) else "")
        r = re.match(r"^\s+(.*?)\s+\((.*)\)$", rest)
        if r:
            ref = re.search(r"\(([^)]+)\)$", r.group(1))
            why[m.group(2)] = (ref.group(1) if ref else r.group(1).split("/")[-1], r.group(2))
    chain, seen = [], set()
    while member in why and member not in seen:
        seen.add(member)
        ref, sym = why[member]
        chain.append(f"{member} <- {ref or 'the linker command line'} ({sym})")
        member = ref
    return chain


def main() -> int:
    bin_path, budget_path = sys.argv[1], sys.argv[2]
    size = os.path.getsize(bin_path)
    budget = json.load(open(budget_path))["app_max_bytes"]
    pct = 100.0 * size / budget
    print(f"esp32 image: {size} bytes / budget {budget} ({pct:.1f}%)")
    failed = False
    if size > budget:
        print("FAIL: image exceeds budget", file=sys.stderr)
        failed = True
    if len(sys.argv) > 3:
        chain = inclusion_chain(open(sys.argv[3], errors="replace").read(), LOCALE_MEMBER)
        if chain:
            print("FAIL: the image links libstdc++'s std::locale machinery (~150K). "
                  "Inclusion chain, innermost first:", file=sys.stderr)
            for step in chain:
                print(f"        {step}", file=sys.stderr)
            print("      Replace the stream, <regex> or std::filesystem use at the end of the "
                  "chain with text_io.h, helix_regex.h or helix_fs.h.", file=sys.stderr)
            failed = True
        map_text = open(sys.argv[3], errors="replace").read()
        eh_chain = inclusion_chain(map_text, EH_MEMBER)
        eh_frame = EH_FRAME_SECTION.search(map_text)
        if eh_chain or (eh_frame and int(eh_frame.group(1), 16) > 0):
            print("FAIL: the image links exception-handling support; the firmware is built "
                  "without exceptions.", file=sys.stderr)
            for step in eh_chain:
                print(f"        {step}", file=sys.stderr)
            if eh_frame:
                print(f"        .eh_frame output section: {int(eh_frame.group(1), 16)} bytes",
                      file=sys.stderr)
            print("      Check CONFIG_COMPILER_CXX_EXCEPTIONS in sdkconfig.defaults and the "
                  "object at the end of the chain.", file=sys.stderr)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

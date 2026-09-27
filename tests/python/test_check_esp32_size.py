# SPDX-License-Identifier: GPL-3.0-or-later
"""scripts/check_esp32_size.py: the budget and the std::locale map check."""
import json
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[2] / "scripts" / "check_esp32_size.py"

# The archive-member section of a GNU ld map, in the shape the ESP-IDF link writes.
MAP_WITH_LOCALE = """\
Archive member included to satisfy reference by file (symbol)

/x/libstdc++.a(locale_init.o)
                              /x/libstdc++.a(ios.o) (_ZNSt6localeC1Ev)
/x/libstdc++.a(ios.o)         /x/libstdc++.a(fs_path.o) (_ZNSt8ios_baseC2Ev)
/x/libstdc++.a(fs_path.o)
                              esp-idf/helixapp/libhelixapp.a(filament_catalog.cpp.obj) (_ZNSt10filesystem7__cxx114path5_ListC1Ev)

Discarded input sections
"""

MAP_WITHOUT_LOCALE = """\
Archive member included to satisfy reference by file (symbol)

/x/libstdc++.a(string-inst.o)
                              esp-idf/helixapp/libhelixapp.a(app_boot.cpp.obj) (_ZNKSt7__cxx1112basic_string)

Discarded input sections
"""


def run(tmp_path, size, budget, map_text=None):
    binp = tmp_path / "app.bin"
    binp.write_bytes(b"\0" * size)
    budgetp = tmp_path / "budget.json"
    budgetp.write_text(json.dumps({"app_max_bytes": budget}))
    args = [sys.executable, str(SCRIPT), str(binp), str(budgetp)]
    if map_text is not None:
        mapp = tmp_path / "app.map"
        mapp.write_text(map_text)
        args.append(str(mapp))
    return subprocess.run(args, capture_output=True, text=True)


def test_under_budget_passes(tmp_path):
    assert run(tmp_path, 100, 200).returncode == 0


def test_over_budget_fails(tmp_path):
    r = run(tmp_path, 300, 200)
    assert r.returncode == 1
    assert "exceeds budget" in r.stderr


def test_linked_locale_fails_and_names_the_chain(tmp_path):
    r = run(tmp_path, 100, 200, MAP_WITH_LOCALE)
    assert r.returncode == 1
    assert "locale_init.o <- ios.o" in r.stderr
    assert "fs_path.o <- filament_catalog.cpp.obj" in r.stderr


def test_map_without_locale_passes(tmp_path):
    assert run(tmp_path, 100, 200, MAP_WITHOUT_LOCALE).returncode == 0


# A link with exceptions on: the personality routine pulled by an object compiled
# with them, and IDF's .eh_frame output section.
MAP_WITH_EH = """\
Archive member included to satisfy reference by file (symbol)

/x/libstdc++.a(eh_personality.o)
                              esp-idf/helixapp/libhelixapp.a(config.cpp.obj) (__gxx_personality_v0)

Discarded input sections

.eh_frame       0x3c5eb9f4    0x6c264
"""

MAP_EMPTY_EH_FRAME = MAP_WITHOUT_LOCALE + """
.eh_frame       0x3c5eb9f4    0x0
"""


def test_linked_exception_support_fails_and_names_the_object(tmp_path):
    r = run(tmp_path, 100, 200, MAP_WITH_EH)
    assert r.returncode == 1
    assert "eh_personality.o <- config.cpp.obj (__gxx_personality_v0)" in r.stderr
    assert ".eh_frame output section: 442980 bytes" in r.stderr


def test_no_exception_support_and_an_empty_eh_frame_pass(tmp_path):
    assert run(tmp_path, 100, 200, MAP_WITHOUT_LOCALE).returncode == 0
    assert run(tmp_path, 100, 200, MAP_EMPTY_EH_FRAME).returncode == 0

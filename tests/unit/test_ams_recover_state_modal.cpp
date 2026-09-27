// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_ams_recover_state_modal.h"

#include "../catch_amalgamated.hpp"

using helix::AmsSystemInfo;
using helix::RecoverStateRequest;
using helix::ui::AmsRecoverStateModal;

namespace {
AmsRecoverStateModal::Choices choices(bool has_bypass) {
    AmsRecoverStateModal::Choices c;
    c.tool_count = 4;
    c.slot_count = 4;
    c.has_bypass = has_bypass;
    return c;
}
} // namespace

TEST_CASE("Recover state modal pre-fills from the live state", "[ams][recover_state]") {
    AmsSystemInfo info;
    info.current_tool = 2;
    info.current_slot = 3;
    info.filament_loaded = true;

    auto req = AmsRecoverStateModal::prefill(info, false);
    CHECK(req.tool == 2);
    CHECK(req.slot == 3);
    CHECK_FALSE(req.bypass);
    CHECK(req.loaded == std::optional<bool>(true));

    // -1 and the -2 bypass sentinel both read as unknown; bypass is its own flag.
    info.current_tool = -2;
    info.current_slot = -2;
    info.filament_loaded = false;
    req = AmsRecoverStateModal::prefill(info, true);
    CHECK(req.tool == -1);
    CHECK(req.slot == -1);
    CHECK(req.bypass);
    CHECK(req.loaded == std::optional<bool>(false));
}

TEST_CASE("Recover state modal selections map to 1-based rows after Unknown",
          "[ams][recover_state]") {
    RecoverStateRequest req;
    req.tool = 0;
    req.slot = 3;
    req.loaded = false;
    auto s = AmsRecoverStateModal::selection_for(req, choices(true));
    CHECK(s.tool == 1);
    CHECK(s.slot == 4);
    CHECK(s.loaded == 2);

    RecoverStateRequest unknown;
    s = AmsRecoverStateModal::selection_for(unknown, choices(true));
    CHECK(s.tool == 0);
    CHECK(s.slot == 0);
    CHECK(s.loaded == 0);

    RecoverStateRequest bypass;
    bypass.bypass = true;
    CHECK(AmsRecoverStateModal::selection_for(bypass, choices(true)).slot == 5);
    // No bypass row to select: fall back to Unknown rather than an out-of-range row.
    CHECK(AmsRecoverStateModal::selection_for(bypass, choices(false)).slot == 0);
}

TEST_CASE("Recover state modal round-trips every selection", "[ams][recover_state]") {
    const auto c = choices(true);
    for (uint32_t tool = 0; tool <= 4; ++tool) {
        for (uint32_t slot = 0; slot <= 5; ++slot) {
            for (uint32_t loaded = 0; loaded <= 2; ++loaded) {
                AmsRecoverStateModal::Selection in{tool, slot, loaded};
                const auto req = AmsRecoverStateModal::request_for(in, c);
                INFO("tool=" << tool << " slot=" << slot << " loaded=" << loaded);
                if (slot == 5) {
                    CHECK(req.bypass);
                    CHECK(req.tool == -1);
                    CHECK(req.slot == -1);
                } else {
                    CHECK_FALSE(req.bypass);
                    CHECK(req.tool == static_cast<int>(tool) - 1);
                    CHECK(req.slot == static_cast<int>(slot) - 1);
                }
                CHECK(req.loaded.has_value() == (loaded != 0));
                const auto out = AmsRecoverStateModal::selection_for(req, c);
                CHECK(out.slot == in.slot);
                CHECK(out.loaded == in.loaded);
                if (slot != 5) {
                    CHECK(out.tool == in.tool);
                }
            }
        }
    }
}

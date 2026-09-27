// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string>
#include <string_view>

namespace helix {

/// Stored value of a Quick Actions slot that toggles the printer light rather
/// than running a standard macro. Slot values are StandardMacros slot names
/// (lowercase identifiers such as "clean_nozzle"), never Klipper macro names,
/// and no slot name contains ':', so this id cannot collide with either.
inline constexpr std::string_view kQuickSlotLight = "builtin:light";

enum class QuickSlotKind { Empty, Macro, Light };

struct QuickSlotInput {
    /// The user has written this slot, including clearing it to empty. A slot
    /// never written reads its default and may take the light by default.
    bool user_set = false;
    /// Stored value, or the default when not user_set.
    std::string value;
    /// The value names a standard macro that renders a button (resolved, or
    /// assigned-but-missing, which shows greyed out).
    bool macro_renders = false;
};

/// What each Quick Actions slot shows. The light shows where assigned while an
/// LED is controllable; with none assigned, it fills the first slot the user
/// never set and that would otherwise be empty.
inline std::array<QuickSlotKind, 4> resolve_quick_slots(const std::array<QuickSlotInput, 4>& in,
                                                        bool led_controllable) {
    std::array<QuickSlotKind, 4> out{};
    bool light_assigned = false;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i].value == kQuickSlotLight) {
            out[i] = led_controllable ? QuickSlotKind::Light : QuickSlotKind::Empty;
            light_assigned = true;
        } else {
            out[i] = in[i].macro_renders ? QuickSlotKind::Macro : QuickSlotKind::Empty;
        }
    }
    if (led_controllable && !light_assigned) {
        for (size_t i = 0; i < in.size(); ++i) {
            if (!in[i].user_set && out[i] == QuickSlotKind::Empty) {
                out[i] = QuickSlotKind::Light;
                break;
            }
        }
    }
    return out;
}

} // namespace helix

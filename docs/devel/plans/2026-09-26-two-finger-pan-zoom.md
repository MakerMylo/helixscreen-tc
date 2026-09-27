# Two-Finger Pan and Pinch Zoom Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pinch to zoom anchored under the fingers, and two-finger drag to pan, on both the 3D G-code preview and the 3D bed mesh, on capacitive screens.

**Architecture:** One small module (`view_gestures`) reads LVGL's PINCH / TWO_FINGERS_SWIPE recognizer data and turns it into per-frame `{pan, zoom, anchor}` steps through a pure function. Each view maps a step onto its own camera: `GCodeCamera` gains `pan_pixels`/`zoom_at`; the bed mesh gains a screen-space `zoom`/`pan` applied after the perspective divide. Each widget keeps its own "two-finger occurred" latch next to its existing tap/rotate/long-press state. DRM and fbdev both configure gesture thresholds through one backend helper.

**Tech Stack:** C++17, LVGL 9.5 gesture recognition (evdev multi-touch), glm, Catch2 v3, pure Makefile.

**Spec:** `docs/devel/plans/2026-09-26-two-finger-pan-zoom-design.md` (promoted from `docs/superpowers/` in Task 0).

## Global Constraints

- Capacitive (multi-touch) screens only; single-touch screens get no new behaviour and no fallback UI.
- 3D views only: G-code viewer 2D mode and bed mesh 2D heatmap are untouched.
- No LVGL patch. Private LVGL struct reads happen only inside `read_two_finger_sample` (`src/ui/view_gestures.cpp`).
- Recognizer thresholds: pinch up `1.15f`, pinch down `0.85f`, rotation `3.14f` rad, on every evdev pointer (DRM and fbdev).
- Per-frame zoom filter: a frame ratio outside the open interval (0.7, 1.4), or a non-positive scale, zooms 1.0 that frame (pan still applies).
- Bed mesh zoom range `[1.0, 8.0]`; zoom 1.0 implies pan (0, 0).
- G-code zoom keeps the existing `GCodeCamera::zoom` clamp `[0.1, 100]`.
- Rotation sensitivity `0.5` deg/px, single definition `helix::ui::kRotateDegreesPerPixel`.
- spdlog only; SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later` plus the `// Copyright (C) 2025-2026 356C LLC` line on new source files.
- Comments describe the code as it is now: no history, no "used to", no SHAs (CLAUDE.md § Comments).
- New `src/**/*.cpp` and `tests/unit/*.cpp` are picked up by wildcard; no build registration.

## Review Focus

1. **Both fingers lift in one input poll, so LVGL never delivers ENDED.** The next touch must start from clean totals, or its first frame jumps by the stale cumulative delta. Pinned: Task 1's "ONGOING discards stale totals" test, plus the PRESSED reset in Tasks 4 and 6.
2. **A finger left down after a pinch.** One-finger rotate must not resume (and jump from a stale `last_drag_pos`) until every finger lifts. Pinned: the latch in Tasks 4 and 6, and Preston's hands-on check in Task 6.5.
3. **Zoom at a clamp.** `zoom_at` at the G-code 100x limit, or the bed mesh 8x limit, must not drift the view: the anchor math uses the zoom actually applied. Pinned: clamp tests in Tasks 3 and 5.
4. **A zero-size viewport** (a widget laid out while hidden) must not turn the camera target into NaN, which would blank the preview until the next file load. Pinned: Task 3's zero-viewport test.
5. **New bed bounds while zoomed.** The bed mesh auto-fit (`calibrate_fov_scale`, `compute_initial_centering`) projects through the zoomed function, so it would fit the magnified mesh and cancel the zoom. `set_bounds` must reset zoom first. Pinned: Task 5's `set_bounds` reset test.

---

### Task 0: Worktree and plan docs

**Files:**
- Create: `docs/devel/plans/2026-09-26-two-finger-pan-zoom-design.md` (copy of `docs/superpowers/2026-09-26-two-finger-pan-zoom-design.md`)
- Create: `docs/devel/plans/2026-09-26-two-finger-pan-zoom.md` (copy of this file, with the Spec line unchanged)

- [ ] **Step 1: Check the box and create the worktree**

```bash
cd /home/pbrown/Code/Printing/helixscreen
pgrep -x -d' ' 'make|clang++|cc1plus'; free -h
scripts/setup-worktree.sh feature/two-finger-pan-zoom
cd .worktrees/two-finger-pan-zoom
scripts/helix-claim take worktree:two-finger-pan-zoom "two-finger pan/zoom" --note "gesture work, both 3D views"
```

- [ ] **Step 2: Copy the spec and plan in, and commit**

```bash
cp ../../docs/superpowers/2026-09-26-two-finger-pan-zoom-design.md docs/devel/plans/
cp ../../docs/superpowers/2026-09-26-two-finger-pan-zoom.md docs/devel/plans/
git add docs/devel/plans/2026-09-26-two-finger-pan-zoom-design.md docs/devel/plans/2026-09-26-two-finger-pan-zoom.md
git commit -m "docs(plans): two-finger pan and pinch zoom design and plan"
git show --stat HEAD
```

(`git add` is fine here: this worktree is private. The shared-tree no-`git add` rule is for the main tree.)

---

### Task 1: `view_gestures` module

**Files:**
- Create: `include/view_gestures.h`
- Create: `src/ui/view_gestures.cpp`
- Test: `tests/unit/test_view_gestures.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces (namespace `helix::ui`):
  - `inline constexpr float kRotateDegreesPerPixel = 0.5f;`
  - `struct TwoFingerSample { enum class Kind { Pinch, Pan } kind; enum class Phase { Ongoing, Recognized, Ended } phase; float delta_x, delta_y, scale; int start_x, start_y; };`
  - `struct TwoFingerState { float last_dx = 0, last_dy = 0, last_scale = 1; };`
  - `struct TwoFingerStep { float pan_dx = 0, pan_dy = 0, zoom = 1; int anchor_x = 0, anchor_y = 0; bool active = false, ended = false; };`
  - `TwoFingerStep two_finger_step(const TwoFingerSample& s, TwoFingerState& st);`
  - `#if LV_USE_GESTURE_RECOGNITION` `std::optional<TwoFingerSample> read_two_finger_sample(lv_event_t* e);`

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_view_gestures.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "view_gestures.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui;
using Catch::Approx;
using Kind = TwoFingerSample::Kind;
using Phase = TwoFingerSample::Phase;

namespace {
TwoFingerSample sample(Kind kind, Phase phase, float dx, float dy, float scale, int sx = 100,
                       int sy = 100) {
    return TwoFingerSample{kind, phase, dx, dy, scale, sx, sy};
}
} // namespace

TEST_CASE("two_finger_step: ONGOING is active with no motion", "[gesture]") {
    TwoFingerState st;
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Ongoing, 5, 5, 1.05f), st);
    CHECK(s.active);
    CHECK_FALSE(s.ended);
    CHECK(s.pan_dx == 0.0f);
    CHECK(s.pan_dy == 0.0f);
    CHECK(s.zoom == 1.0f);
}

TEST_CASE("two_finger_step: first recognized frame catches up pre-recognition motion",
          "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pan, Phase::Ongoing, 0, 0, 1.0f), st);
    auto s = two_finger_step(sample(Kind::Pan, Phase::Recognized, 60, -20, 1.0f), st);
    CHECK(s.active);
    CHECK(s.pan_dx == Approx(60.0f));
    CHECK(s.pan_dy == Approx(-20.0f));
    CHECK(s.zoom == 1.0f);
}

TEST_CASE("two_finger_step: per-frame pan deltas sum to the cumulative translation",
          "[gesture]") {
    TwoFingerState st;
    float sum_x = 0, sum_y = 0;
    for (float d : {60.0f, 75.0f, 74.0f, 90.0f}) {
        auto s = two_finger_step(sample(Kind::Pan, Phase::Recognized, d, -d / 2, 1.0f), st);
        sum_x += s.pan_dx;
        sum_y += s.pan_dy;
    }
    CHECK(sum_x == Approx(90.0f));
    CHECK(sum_y == Approx(-45.0f));
}

TEST_CASE("two_finger_step: pinch zooms by the frame ratio and pans in the same frame",
          "[gesture]") {
    TwoFingerState st;
    auto a = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.2f), st);
    CHECK(a.zoom == Approx(1.2f));
    auto b = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 10, 4, 1.32f), st);
    CHECK(b.zoom == Approx(1.1f));
    CHECK(b.pan_dx == Approx(10.0f));
    CHECK(b.pan_dy == Approx(4.0f));
}

TEST_CASE("two_finger_step: a pan never zooms, whatever scale LVGL reports", "[gesture]") {
    TwoFingerState st;
    auto s = two_finger_step(sample(Kind::Pan, Phase::Recognized, 5, 0, 1.3f), st);
    CHECK(s.zoom == 1.0f);
    CHECK(s.pan_dx == Approx(5.0f));
}

TEST_CASE("two_finger_step: anchor is the start centre plus the cumulative translation",
          "[gesture]") {
    TwoFingerState st;
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 30, -10, 1.2f, 100, 200), st);
    CHECK(s.anchor_x == 130);
    CHECK(s.anchor_y == 190);
}

TEST_CASE("two_finger_step: an implausible frame ratio holds zoom but still pans",
          "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.0f), st);
    auto jump = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 5, 0, 2.0f), st);
    CHECK(jump.zoom == 1.0f);
    CHECK(jump.pan_dx == Approx(5.0f));
    // The jumped scale becomes the new baseline.
    auto next = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 5, 0, 2.1f), st);
    CHECK(next.zoom == Approx(1.05f));
    // Below the lower bound holds zoom too.
    auto low = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 5, 0, 2.1f * 0.69f), st);
    CHECK(low.zoom == 1.0f);
}

TEST_CASE("two_finger_step: a non-positive scale holds zoom and keeps the baseline",
          "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.0f), st);
    CHECK(two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 0.0f), st).zoom == 1.0f);
    CHECK(two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, -1.0f), st).zoom == 1.0f);
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 0, 0, 1.1f), st);
    CHECK(s.zoom == Approx(1.1f));
}

TEST_CASE("two_finger_step: ENDED reports ended and resets totals", "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pinch, Phase::Recognized, 40, 40, 1.3f), st);
    auto end = two_finger_step(sample(Kind::Pinch, Phase::Ended, 40, 40, 1.3f), st);
    CHECK(end.ended);
    CHECK_FALSE(end.active);
    CHECK(end.pan_dx == 0.0f);
    CHECK(end.zoom == 1.0f);
    CHECK(st.last_dx == 0.0f);
    CHECK(st.last_scale == 1.0f);
}

TEST_CASE("two_finger_step: ONGOING discards stale totals from a gesture that never ended",
          "[gesture]") {
    TwoFingerState st;
    two_finger_step(sample(Kind::Pan, Phase::Recognized, 80, 0, 1.0f), st);
    two_finger_step(sample(Kind::Pinch, Phase::Ongoing, 0, 0, 1.0f), st);
    auto s = two_finger_step(sample(Kind::Pinch, Phase::Recognized, 10, 0, 1.2f), st);
    CHECK(s.pan_dx == Approx(10.0f));
    CHECK(s.zoom == Approx(1.2f));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[gesture]'`
Expected: compile failure, `view_gestures.h: No such file or directory`.

- [ ] **Step 3: Write the header**

`include/view_gestures.h`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl.h"

#include <optional>

namespace helix::ui {

/// One-finger drag rotation for the 3D views (G-code preview, bed mesh).
inline constexpr float kRotateDegreesPerPixel = 0.5f;

/// One frame of an LVGL two-finger gesture, in plain types.
struct TwoFingerSample {
    enum class Kind { Pinch, Pan } kind;
    enum class Phase { Ongoing, Recognized, Ended } phase;
    float delta_x; ///< Cumulative centre translation since the gesture began, px
    float delta_y;
    float scale;   ///< Cumulative spread, 1.0 = unchanged. Ignored for Pan.
    int start_x;   ///< Two-finger centre when the gesture began, screen px
    int start_y;
};

/// Per-widget running totals for the gesture in progress.
struct TwoFingerState {
    float last_dx = 0.0f;
    float last_dy = 0.0f;
    float last_scale = 1.0f;
};

/// What a view applies this frame: pan first, then zoom about the anchor.
struct TwoFingerStep {
    float pan_dx = 0.0f; ///< Screen px to move the content
    float pan_dy = 0.0f;
    float zoom = 1.0f;   ///< Multiplicative zoom this frame
    int anchor_x = 0;    ///< Live two-finger centre, screen px
    int anchor_y = 0;
    bool active = false; ///< Two fingers are down
    bool ended = false;  ///< The gesture finished this frame
};

/// Turn LVGL's cumulative totals into this frame's pan and zoom. Pure.
TwoFingerStep two_finger_step(const TwoFingerSample& s, TwoFingerState& st);

#if LV_USE_GESTURE_RECOGNITION
/// Read the pinch or two-finger swipe carried by an LV_EVENT_GESTURE.
/// nullopt for any other gesture (including one-finger swipes).
std::optional<TwoFingerSample> read_two_finger_sample(lv_event_t* e);
#endif

} // namespace helix::ui
```

- [ ] **Step 4: Write the implementation**

`src/ui/view_gestures.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "view_gestures.h"

#if LV_USE_GESTURE_RECOGNITION
#include "lvgl/src/indev/lv_indev_gesture_private.h" // info->delta_x/y, center: no public getter
#endif

#include <cmath>

namespace helix::ui {

namespace {
// Normal per-frame pinch ratios sit near 0.85-1.15; anything outside this
// open interval is a recognizer restart, not finger motion.
constexpr float kMinFrameZoom = 0.7f;
constexpr float kMaxFrameZoom = 1.4f;
} // namespace

TwoFingerStep two_finger_step(const TwoFingerSample& s, TwoFingerState& st) {
    TwoFingerStep out;
    switch (s.phase) {
    case TwoFingerSample::Phase::Ended:
        st = {};
        out.ended = true;
        return out;
    case TwoFingerSample::Phase::Ongoing:
        // Every gesture passes through ONGOING, so totals from a gesture whose
        // ENDED was never delivered cannot leak into this one.
        st = {};
        out.active = true;
        return out;
    case TwoFingerSample::Phase::Recognized:
        break;
    }

    out.active = true;
    out.pan_dx = s.delta_x - st.last_dx;
    out.pan_dy = s.delta_y - st.last_dy;
    st.last_dx = s.delta_x;
    st.last_dy = s.delta_y;

    if (s.kind == TwoFingerSample::Kind::Pinch && s.scale > 0.0f) {
        const float ratio = s.scale / st.last_scale;
        if (ratio > kMinFrameZoom && ratio < kMaxFrameZoom) {
            out.zoom = ratio;
        }
        st.last_scale = s.scale;
    }

    out.anchor_x = s.start_x + static_cast<int>(std::lround(s.delta_x));
    out.anchor_y = s.start_y + static_cast<int>(std::lround(s.delta_y));
    return out;
}

#if LV_USE_GESTURE_RECOGNITION
std::optional<TwoFingerSample> read_two_finger_sample(lv_event_t* e) {
    lv_indev_gesture_type_t type = lv_event_get_gesture_type(e);
    if (type != LV_INDEV_GESTURE_PINCH && type != LV_INDEV_GESTURE_TWO_FINGERS_SWIPE) {
        // Nothing recognized yet: two fingers down shows as the pinch recognizer's ONGOING.
        type = LV_INDEV_GESTURE_PINCH;
    }
    lv_indev_gesture_recognizer_t* r = lv_indev_get_gesture_recognizer(e, type);
    if (!r || !r->info) {
        return std::nullopt;
    }

    TwoFingerSample s{};
    switch (r->state) {
    case LV_INDEV_GESTURE_STATE_ONGOING:
        s.phase = TwoFingerSample::Phase::Ongoing;
        break;
    case LV_INDEV_GESTURE_STATE_RECOGNIZED:
        s.phase = TwoFingerSample::Phase::Recognized;
        break;
    case LV_INDEV_GESTURE_STATE_ENDED:
    case LV_INDEV_GESTURE_STATE_CANCELED:
        s.phase = TwoFingerSample::Phase::Ended;
        break;
    default:
        return std::nullopt;
    }

    const bool pinch = type == LV_INDEV_GESTURE_PINCH;
    s.kind = pinch ? TwoFingerSample::Kind::Pinch : TwoFingerSample::Kind::Pan;
    s.delta_x = r->info->delta_x;
    s.delta_y = r->info->delta_y;
    s.scale = pinch ? r->scale : 1.0f;
    s.start_x = r->info->center.x;
    s.start_y = r->info->center.y;
    return s;
}
#endif

} // namespace helix::ui
```

- [ ] **Step 5: Run the tests**

Run: `make t F='[gesture]'`
Expected: all 10 test cases pass.

- [ ] **Step 6: Commit**

```bash
git add include/view_gestures.h src/ui/view_gestures.cpp tests/unit/test_view_gestures.cpp
git commit -m "feat(ui): view_gestures turns LVGL pinch and two-finger swipe into per-frame pan and zoom steps"
git show --stat HEAD
```

---

### Task 2: One gesture-threshold helper for DRM and fbdev

**Files:**
- Modify: `include/display_backend.h` (public section of `class DisplayBackend`, beside `lvgl_rotation_degrees`)
- Modify: `src/api/display_backend.cpp`
- Modify: `src/api/display_backend_drm.cpp` (the two `#if LV_USE_GESTURE_RECOGNITION` threshold blocks, currently ~640-643 and ~668-671)
- Modify: `src/api/display_backend_fbdev.cpp#create_input_pointer` (after the `touch_ == nullptr` check)
- Test: `tests/unit/test_touch_gesture_config.cpp`

**Interfaces:**
- Produces: `static void DisplayBackend::configure_touch_gestures(lv_indev_t* indev);`

- [ ] **Step 1: Write the failing test**

`tests/unit/test_touch_gesture_config.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display_backend.h"

#include "lvgl/src/indev/lv_indev_gesture_private.h"
#include "lvgl/src/indev/lv_indev_private.h"

#include "../catch_amalgamated.hpp"
#include "../lvgl_test_fixture.h"

using Catch::Approx;

#if LV_USE_GESTURE_RECOGNITION
TEST_CASE_METHOD(LVGLTestFixture,
                 "configure_touch_gestures: pinch thresholds set, rotate out of reach",
                 "[display][gesture]") {
    lv_indev_t* indev = lv_indev_create();
    REQUIRE(indev != nullptr);

    DisplayBackend::configure_touch_gestures(indev);

    const auto* pinch = indev->recognizers[LV_INDEV_GESTURE_PINCH].config;
    REQUIRE(pinch != nullptr);
    CHECK(pinch->pinch_up_threshold == Approx(1.15f));
    CHECK(pinch->pinch_down_threshold == Approx(0.85f));

    const auto* rotate = indev->recognizers[LV_INDEV_GESTURE_ROTATE].config;
    REQUIRE(rotate != nullptr);
    CHECK(rotate->rotation_angle_rad_threshold == Approx(3.14f));

    lv_indev_delete(indev);
}

TEST_CASE_METHOD(LVGLTestFixture, "configure_touch_gestures: null indev is a no-op",
                 "[display][gesture]") {
    DisplayBackend::configure_touch_gestures(nullptr);
    SUCCEED("no crash");
}
#endif
```

If `DisplayBackend` sits inside a namespace in `include/display_backend.h`, qualify it (read the header; line 65 opens `namespace helix::ui`, so check where it closes before line 207).

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[display][gesture]'`
Expected: compile failure, `no member named 'configure_touch_gestures'`.

- [ ] **Step 3: Declare and define the helper**

In `include/display_backend.h`, public section of `DisplayBackend`:

```cpp
    /// Gesture thresholds for an evdev multi-touch pointer. ROTATE is pushed out of
    /// reach so PINCH and two-finger pan are the only two-finger gestures that win.
    static void configure_touch_gestures(lv_indev_t* indev);
```

In `src/api/display_backend.cpp`:

```cpp
void DisplayBackend::configure_touch_gestures(lv_indev_t* indev) {
#if LV_USE_GESTURE_RECOGNITION
    if (!indev) {
        return;
    }
    lv_indev_set_pinch_up_threshold(indev, 1.15f);
    lv_indev_set_pinch_down_threshold(indev, 0.85f);
    lv_indev_set_rotation_rad_threshold(indev, 3.14f);
#else
    (void)indev;
#endif
}
```

- [ ] **Step 4: Call it from both backends**

In `src/api/display_backend_drm.cpp`, replace **both** blocks

```cpp
#if LV_USE_GESTURE_RECOGNITION
            lv_indev_set_pinch_up_threshold(pointer_, 1.15f);
            lv_indev_set_pinch_down_threshold(pointer_, 0.85f);
            lv_indev_set_rotation_rad_threshold(pointer_, 3.14f);
#endif
```

with

```cpp
            configure_touch_gestures(pointer_);
```

In `src/api/display_backend_fbdev.cpp#create_input_pointer`, directly after the block that returns on `touch_ == nullptr`:

```cpp
    configure_touch_gestures(touch_);
```

- [ ] **Step 5: Run the test and a syntax check of both backends**

Run: `make t F='[display][gesture]'` → both cases pass.
Run: `scripts/syntax_check.py src/api/display_backend_drm.cpp src/api/display_backend_fbdev.cpp` → no errors. (The DRM backend may be compiled out on this host; syntax_check uses its compile_commands flags.)
Run: `grep -n "lv_indev_set_pinch" src/api/*.cpp` → matches only in `display_backend.cpp`.

- [ ] **Step 6: Commit**

```bash
git add include/display_backend.h src/api/display_backend.cpp src/api/display_backend_drm.cpp src/api/display_backend_fbdev.cpp tests/unit/test_touch_gesture_config.cpp
git commit -m "fix(input): fbdev touch gets the same pinch/rotate thresholds as DRM, via one helper"
git show --stat HEAD
```

---

### Task 3: `GCodeCamera::pan_pixels` and `zoom_at`

**Files:**
- Modify: `include/gcode_camera.h` (public API after `zoom`; private helper after `compute_camera_position`)
- Modify: `src/rendering/gcode_camera.cpp` (after `GCodeCamera::zoom`)
- Test: `tests/unit/test_gcode_camera_pan_zoom.cpp`

**Interfaces:**
- Consumes: existing `GCodeCamera::pan(float, float)`, `zoom(float)`, `set_viewport_size(int, int)`, `get_view_projection_matrix()`.
- Produces:
  - `void GCodeCamera::pan_pixels(float dx, float dy);` content follows a screen drag of (dx, dy) px.
  - `void GCodeCamera::zoom_at(float factor, float anchor_x, float anchor_y);` anchor in widget-local px.

Orthographic facts (from `GCodeCamera::update_matrices`): half-height is `distance_ / (2 * zoom_level_)`, so one pixel is `distance_ / (zoom_level_ * viewport_height_)` world units. The renderer maps NDC to screen with a Y flip (`src/rendering/gcode_renderer.cpp`: `screen_y = (1.0f - ndc.y) * 0.5f * viewport_height_`).

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_gcode_camera_pan_zoom.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gcode_camera.h"

#include <cmath>
#include <glm/glm.hpp>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;
using Catch::Approx;

namespace {
// Same NDC -> pixel mapping as GCodeRenderer (Y flipped).
glm::vec2 to_screen(const GCodeCamera& cam, glm::vec3 p) {
    glm::vec4 clip = cam.get_view_projection_matrix() * glm::vec4(p, 1.0f);
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {(ndc.x + 1.0f) * 0.5f * cam.get_viewport_width(),
            (1.0f - ndc.y) * 0.5f * cam.get_viewport_height()};
}

GCodeCamera make_camera(float zoom) {
    GCodeCamera cam;
    cam.set_viewport_size(800, 480);
    cam.set_zoom_level(zoom);
    return cam;
}
} // namespace

TEST_CASE("GCodeCamera::pan_pixels moves content exactly with the finger",
          "[gcode][camera]") {
    for (float zoom : {1.4f, 6.0f}) {
        auto cam = make_camera(zoom);
        const glm::vec3 p{12.0f, -7.0f, 3.0f};
        const glm::vec2 before = to_screen(cam, p);
        cam.pan_pixels(37.0f, -21.0f);
        const glm::vec2 after = to_screen(cam, p);
        CHECK(after.x - before.x == Approx(37.0f).margin(0.5));
        CHECK(after.y - before.y == Approx(-21.0f).margin(0.5));
    }
}

TEST_CASE("GCodeCamera::zoom_at keeps the world point under the anchor fixed",
          "[gcode][camera]") {
    auto cam = make_camera(1.4f);
    const glm::vec3 p{20.0f, 15.0f, 0.0f};
    const glm::vec2 anchor = to_screen(cam, p);
    cam.zoom_at(1.5f, anchor.x, anchor.y);
    cam.zoom_at(1.3f, anchor.x, anchor.y);
    const glm::vec2 after = to_screen(cam, p);
    CHECK(after.x == Approx(anchor.x).margin(0.5));
    CHECK(after.y == Approx(anchor.y).margin(0.5));
    CHECK(cam.get_zoom_level() == Approx(1.4f * 1.5f * 1.3f));
}

TEST_CASE("GCodeCamera::zoom_at at the zoom clamp does not drift the view",
          "[gcode][camera]") {
    auto cam = make_camera(100.0f); // at the max clamp
    const glm::vec3 target_before = cam.get_target();
    cam.zoom_at(1.3f, 50.0f, 60.0f);
    const glm::vec3 target_after = cam.get_target();
    CHECK(cam.get_zoom_level() == Approx(100.0f));
    CHECK(target_after.x == Approx(target_before.x).margin(1e-4));
    CHECK(target_after.y == Approx(target_before.y).margin(1e-4));
    CHECK(target_after.z == Approx(target_before.z).margin(1e-4));
}

TEST_CASE("GCodeCamera pan/zoom on a zero-height viewport leave the target finite",
          "[gcode][camera]") {
    GCodeCamera cam;
    cam.set_viewport_size(800, 0);
    cam.pan_pixels(10.0f, 10.0f);
    cam.zoom_at(1.2f, 10.0f, 10.0f);
    const glm::vec3 t = cam.get_target();
    CHECK(std::isfinite(t.x));
    CHECK(std::isfinite(t.y));
    CHECK(std::isfinite(t.z));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[gcode][camera]'`
Expected: compile failure, `no member named 'pan_pixels'`.

- [ ] **Step 3: Declare**

`include/gcode_camera.h`, public, after `void zoom(float factor);`:

```cpp
    /// Move the view so content follows a screen-space drag of (dx, dy) pixels.
    void pan_pixels(float dx, float dy);

    /// Zoom by factor, keeping the world point under widget-local pixel
    /// (anchor_x, anchor_y) where it is on screen.
    void zoom_at(float factor, float anchor_x, float anchor_y);
```

Private, after `glm::vec3 compute_camera_position() const;`:

```cpp
    /// World units per screen pixel of the orthographic view; 0 when undefined.
    float world_units_per_pixel() const;
```

- [ ] **Step 4: Implement**

`src/rendering/gcode_camera.cpp`, after `GCodeCamera::zoom`:

```cpp
float GCodeCamera::world_units_per_pixel() const {
    if (viewport_height_ <= 0 || zoom_level_ <= 0.0f) {
        return 0.0f;
    }
    return distance_ / (zoom_level_ * static_cast<float>(viewport_height_));
}

void GCodeCamera::pan_pixels(float dx, float dy) {
    const float wpp = world_units_per_pixel();
    if (wpp <= 0.0f) {
        return;
    }
    // Content follows the finger, so the target moves the opposite way. Screen Y
    // grows downward while camera-up grows upward.
    pan(-dx * wpp, dy * wpp);
}

void GCodeCamera::zoom_at(float factor, float anchor_x, float anchor_y) {
    const float before = world_units_per_pixel();
    if (before <= 0.0f) {
        return;
    }
    zoom(factor);
    const float shift = before - world_units_per_pixel();
    const float off_x = anchor_x - static_cast<float>(viewport_width_) * 0.5f;
    const float off_y = anchor_y - static_cast<float>(viewport_height_) * 0.5f;
    pan(off_x * shift, -off_y * shift);
}
```

(`shift` comes from the zoom level actually reached, so at a clamp it is 0 and the view does not drift.)

- [ ] **Step 5: Run the tests, plus the existing projection tag**

Run: `make t F='[gcode][camera]'` → 4 cases pass.
Run: `./build/bin/helix-tests '[projection]'` → still green.

- [ ] **Step 6: Commit**

```bash
git add include/gcode_camera.h src/rendering/gcode_camera.cpp tests/unit/test_gcode_camera_pan_zoom.cpp
git commit -m "feat(gcode): GCodeCamera pans by screen pixels and zooms about an anchor point"
git show --stat HEAD
```

---

### Task 4: G-code viewer gestures

**Files:**
- Modify: `src/ui/ui_gcode_viewer.cpp`: constants (~44-50), `GCodeViewerState` gesture fields (~259-268), `gcode_viewer_press_cb`, `gcode_viewer_pressing_cb`, `gcode_viewer_release_cb`, `gcode_viewer_gesture_cb`.

**Interfaces:**
- Consumes: `helix::ui::read_two_finger_sample`, `two_finger_step`, `TwoFingerState`, `kRotateDegreesPerPixel` (Task 1); `GCodeCamera::pan_pixels`, `zoom_at` (Task 3).

No unit test: the latch lives in static widget callbacks. It is covered by hardware verification (Task 8). This task's gate is a clean build, the existing viewer tags, and a mock run showing no regression in one-finger rotate and tap.

- [ ] **Step 1: Include and constant**

Add `#include "view_gestures.h"` with the other project includes. Delete `constexpr float ROTATION_DEGREES_PER_PIXEL = 0.5f;` and in `gcode_viewer_pressing_cb` use:

```cpp
        float delta_azimuth = dx * helix::ui::kRotateDegreesPerPixel;
        float delta_elevation = dy * helix::ui::kRotateDegreesPerPixel;
```

- [ ] **Step 2: Replace the pinch fields in `GCodeViewerState`**

Replace

```cpp
#if LV_USE_GESTURE_RECOGNITION
    float last_pinch_scale{0.0f}; ///< Previous cumulative pinch scale (0 = no reference yet)
    bool is_pinching{false};      ///< True during active pinch gesture (suppresses drag rotation)
    bool pinch_occurred{false};   ///< True if a pinch engaged at any point this touch sequence
#endif
```

with

```cpp
#if LV_USE_GESTURE_RECOGNITION
    helix::ui::TwoFingerState two_finger; ///< Pan/zoom totals for the gesture in progress
    bool two_finger_occurred{false};      ///< Sticky until all fingers lift: gates rotate, tap, long-press
#endif
```

- [ ] **Step 3: `gcode_viewer_press_cb` reset**

Replace the three pinch resets in the `if (!st->is_dragging)` block with:

```cpp
#if LV_USE_GESTURE_RECOGNITION
        st->two_finger = {};
        st->two_finger_occurred = false;
#endif
```

Keep the existing comment above the block but reword its second and third sentences to name the two-finger latch instead of the pinch latch.

- [ ] **Step 4: `gcode_viewer_pressing_cb` suppression**

Replace

```cpp
#if LV_USE_GESTURE_RECOGNITION
    // Suppress drag rotation during pinch-to-zoom to prevent fighting
    if (st->is_pinching) {
        spdlog::debug("[GCode Viewer] PRESSING suppressed (pinching)");
        return;
    }
#endif
```

with

```cpp
#if LV_USE_GESTURE_RECOGNITION
    // No rotation from the first two-finger frame until every finger lifts: a finger
    // left down after a pinch would otherwise jump the camera from a stale position.
    if (st->two_finger_occurred) {
        return;
    }
#endif
```

- [ ] **Step 5: `gcode_viewer_release_cb`: skip only the tap**

Delete the `#if LV_USE_GESTURE_RECOGNITION` block that returns early on `is_pinching || pinch_occurred`. Before the tap test, add:

```cpp
    bool two_finger = false;
#if LV_USE_GESTURE_RECOGNITION
    // A two-finger gesture that ends as a one-finger lift lands as a low-movement
    // release; it must not read as an object tap.
    two_finger = st->two_finger_occurred;
#endif
```

and change the tap condition to

```cpp
    if (!two_finger && !st->gesture_moved && dx < CLICK_THRESHOLD && dy < CLICK_THRESHOLD &&
        has_gcode_data(st)) {
```

The tail (`is_dragging = false`, `set_interaction_mode(false)`, final `lv_obj_invalidate`) now runs after a two-finger gesture too, so the view returns to full resolution.

- [ ] **Step 6: Rewrite `gcode_viewer_gesture_cb`**

Replace the whole function (and its doc comment) with:

```cpp
#if LV_USE_GESTURE_RECOGNITION
/**
 * @brief Two-finger pan and pinch zoom (3D mode only)
 *
 * A pinch zooms about the fingers and pans with them in the same frame; a
 * two-finger swipe only pans. Whichever LVGL recognizes first owns the touch
 * until a finger lifts.
 */
static void gcode_viewer_gesture_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    gcode_viewer_state_t* st = get_state(obj);

    if (!st || st->is_using_2d_mode())
        return;

    const auto sample = helix::ui::read_two_finger_sample(e);
    if (!sample)
        return;

    const helix::ui::TwoFingerStep step = helix::ui::two_finger_step(*sample, st->two_finger);
    if (!step.active)
        return;

    if (!st->two_finger_occurred) {
        st->two_finger_occurred = true;
        if (st->long_press_timer_) {
            lv_timer_delete(st->long_press_timer_);
            st->long_press_timer_ = nullptr;
        }
    }

    if (step.pan_dx == 0.0f && step.pan_dy == 0.0f && step.zoom == 1.0f)
        return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    st->camera_->pan_pixels(step.pan_dx, step.pan_dy);
    st->camera_->zoom_at(step.zoom, static_cast<float>(step.anchor_x - coords.x1),
                         static_cast<float>(step.anchor_y - coords.y1));
    lv_obj_invalidate(obj);
}
#endif
```

- [ ] **Step 7: Verify**

Run: `grep -n "is_pinching\|pinch_occurred\|last_pinch_scale\|ROTATION_DEGREES_PER_PIXEL" src/ui/ui_gcode_viewer.cpp` → no matches.
Run: `scripts/syntax_check.py src/ui/ui_gcode_viewer.cpp` → clean.
Run: `make t F='[gcode]'` → green (the existing viewer tests plus Task 3's).
Mock smoke, per CLAUDE.md's pinned-socket recipe, with `ctl` driving the G-code preview: one-finger drag still rotates, and a tap on an object still selects it. Read the `-vv` log for errors.

- [ ] **Step 8: Commit**

```bash
git add src/ui/ui_gcode_viewer.cpp
git commit -m "feat(gcode): two-finger pan and anchored pinch zoom on the 3D preview; rotate, tap and long-press held off until all fingers lift"
git show --stat HEAD
```

---

### Task 5: Bed mesh zoom and pan in the projection

**Files:**
- Modify: `include/bed_mesh_renderer.h` (constants beside `BED_MESH_DEFAULT_Z_SCALE`, ~98; `bed_mesh_view_state_t`, ~141-164; new API declaration beside `bed_mesh_renderer_set_rotation`, ~243)
- Modify: `include/bed_mesh_projection.h`
- Modify: `src/rendering/bed_mesh_projection.cpp`
- Modify: `src/rendering/bed_mesh_renderer.cpp` (`bed_mesh_renderer_create` defaults ~140-161, `bed_mesh_renderer_set_bounds` ~322-327, `bed_mesh_renderer_set_render_mode` ~1659, new `bed_mesh_renderer_apply_two_finger`)
- Test: `tests/unit/test_bed_mesh_zoom_pan.cpp`

**Interfaces:**
- Produces:
  - `#define BED_MESH_ZOOM_MIN 1.0` and `#define BED_MESH_ZOOM_MAX 8.0`
  - `bed_mesh_view_state_t` fields `double zoom = 1.0; double pan_x = 0.0; double pan_y = 0.0;`
  - `void bed_mesh_projection_pan(bed_mesh_view_state_t* view, double dx, double dy);`
  - `void bed_mesh_projection_zoom_at(bed_mesh_view_state_t* view, double factor, double anchor_x, double anchor_y, int canvas_width, int canvas_height);` (anchor canvas-local px)
  - `void bed_mesh_projection_reset_zoom(bed_mesh_view_state_t* view);`
  - `void bed_mesh_renderer_apply_two_finger(bed_mesh_renderer_t* renderer, double pan_dx, double pan_dy, double zoom, double anchor_x, double anchor_y, int canvas_width, int canvas_height);`

The magnification origin, canvas-local, is the projection's own origin: `ox = canvas_width / 2 + center_offset_x` (integer division, as in the projection), `oy = canvas_height * BED_MESH_Z_ORIGIN_VERTICAL_POS + center_offset_y`. A projected point sits at `o + persp * zoom + pan`, so keeping anchor `a` fixed while zoom goes `z -> z'` needs `pan' = (a - o) - (a - o - pan) * (z' / z)`.

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_bed_mesh_zoom_pan.cpp`:

```cpp
// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bed_mesh_projection.h"
#include "bed_mesh_renderer.h"

#include <cmath>
#include <limits>

#include "../catch_amalgamated.hpp"

using Catch::Approx;

#if HELIX_HAS_BED_MESH_3D
namespace {
constexpr int W = 600;
constexpr int H = 400;

bed_mesh_view_state_t make_view() {
    bed_mesh_view_state_t v{};
    v.angle_x = 0.0;
    v.angle_z = 0.0;
    v.z_scale = 60.0;
    v.fov_scale = 150.0;
    v.camera_distance = 1000.0;
    v.cached_cos_x = 1.0;
    v.cached_sin_x = 0.0;
    v.cached_cos_z = 1.0;
    v.cached_sin_z = 0.0;
    v.trig_cache_valid = true;
    v.center_offset_x = 7;
    v.center_offset_y = -5;
    v.zoom = 1.0;
    v.pan_x = 0.0;
    v.pan_y = 0.0;
    return v;
}

bed_mesh_point_3d_t project(const bed_mesh_view_state_t& v, double x, double y, double z) {
    return bed_mesh_projection_project_3d_to_2d(x, y, z, W, H, &v);
}
} // namespace

TEST_CASE("bed mesh projection: default zoom and pan match the plain perspective formula",
          "[bed_mesh][zoom]") {
    const auto v = make_view();
    const double x = 120.0, y = -80.0, z = 30.0;
    // Rotation is identity: final_y = y*cos_x - z*sin_x = y, final_z = dist - (y*sin_x + z*cos_x)
    const double fz = v.camera_distance - z;
    const int ex = static_cast<int>(W / 2 + (x * v.fov_scale) / fz) + v.center_offset_x;
    const int ey = static_cast<int>(H * BED_MESH_Z_ORIGIN_VERTICAL_POS + (y * v.fov_scale) / fz) +
                   v.center_offset_y;
    const auto p = project(v, x, y, z);
    CHECK(p.screen_x == ex);
    CHECK(p.screen_y == ey);
}

TEST_CASE("bed mesh projection: pan shifts every point by the pan offset", "[bed_mesh][zoom]") {
    auto v = make_view();
    v.zoom = 2.0;
    const auto base = project(v, 50.0, 40.0, 10.0);
    v.pan_x = 30.0;
    v.pan_y = -12.0;
    const auto moved = project(v, 50.0, 40.0, 10.0);
    CHECK(moved.screen_x - base.screen_x == Approx(30).margin(1));
    CHECK(moved.screen_y - base.screen_y == Approx(-12).margin(1));
}

TEST_CASE("bed mesh zoom_at keeps the anchor fixed across successive zooms",
          "[bed_mesh][zoom]") {
    auto v = make_view();
    const auto a = project(v, 150.0, 90.0, 0.0);
    bed_mesh_projection_zoom_at(&v, 1.5, a.screen_x, a.screen_y, W, H);
    bed_mesh_projection_zoom_at(&v, 2.0, a.screen_x, a.screen_y, W, H);
    const auto after = project(v, 150.0, 90.0, 0.0);
    CHECK(v.zoom == Approx(3.0));
    CHECK(after.screen_x == Approx(a.screen_x).margin(1));
    CHECK(after.screen_y == Approx(a.screen_y).margin(1));
}

TEST_CASE("bed mesh zoom_at clamps at 8x and still holds the anchor", "[bed_mesh][zoom]") {
    auto v = make_view();
    const auto a = project(v, -100.0, 60.0, 0.0);
    bed_mesh_projection_zoom_at(&v, 100.0, a.screen_x, a.screen_y, W, H);
    CHECK(v.zoom == Approx(BED_MESH_ZOOM_MAX));
    const auto after = project(v, -100.0, 60.0, 0.0);
    CHECK(after.screen_x == Approx(a.screen_x).margin(1));
    CHECK(after.screen_y == Approx(a.screen_y).margin(1));
}

TEST_CASE("bed mesh zooming out to 1x snaps pan to zero", "[bed_mesh][zoom]") {
    auto v = make_view();
    bed_mesh_projection_zoom_at(&v, 2.0, 100.0, 100.0, W, H);
    bed_mesh_projection_pan(&v, 40.0, -25.0);
    REQUIRE(v.pan_x != 0.0);
    bed_mesh_projection_zoom_at(&v, 0.3, 100.0, 100.0, W, H);
    CHECK(v.zoom == Approx(BED_MESH_ZOOM_MIN));
    CHECK(v.pan_x == 0.0);
    CHECK(v.pan_y == 0.0);
}

TEST_CASE("bed mesh pan is a no-op at 1x", "[bed_mesh][zoom]") {
    auto v = make_view();
    bed_mesh_projection_pan(&v, 40.0, -25.0);
    CHECK(v.pan_x == 0.0);
    CHECK(v.pan_y == 0.0);
}

TEST_CASE("bed mesh zoom_at ignores non-positive and NaN factors", "[bed_mesh][zoom]") {
    auto v = make_view();
    bed_mesh_projection_zoom_at(&v, 2.0, 100.0, 100.0, W, H);
    const auto before = v;
    bed_mesh_projection_zoom_at(&v, 0.0, 100.0, 100.0, W, H);
    bed_mesh_projection_zoom_at(&v, -2.0, 100.0, 100.0, W, H);
    bed_mesh_projection_zoom_at(&v, std::numeric_limits<double>::quiet_NaN(), 100.0, 100.0, W, H);
    CHECK(v.zoom == before.zoom);
    CHECK(v.pan_x == before.pan_x);
    CHECK(v.pan_y == before.pan_y);
}

TEST_CASE("bed mesh renderer: set_bounds and set_render_mode reset zoom and pan",
          "[bed_mesh][zoom]") {
    bed_mesh_renderer_t* r = bed_mesh_renderer_create();
    REQUIRE(r != nullptr);

    bed_mesh_renderer_apply_two_finger(r, 0.0, 0.0, 3.0, 100.0, 100.0, W, H);
    bed_mesh_renderer_apply_two_finger(r, 20.0, 10.0, 1.0, 100.0, 100.0, W, H);
    REQUIRE(bed_mesh_renderer_get_view_state(r)->zoom == Approx(3.0));
    REQUIRE(bed_mesh_renderer_get_view_state(r)->pan_x != 0.0);

    bed_mesh_renderer_set_bounds(r, 0, 235, 0, 235, 10, 225, 10, 225);
    CHECK(bed_mesh_renderer_get_view_state(r)->zoom == 1.0);
    CHECK(bed_mesh_renderer_get_view_state(r)->pan_x == 0.0);
    CHECK(bed_mesh_renderer_get_view_state(r)->pan_y == 0.0);

    bed_mesh_renderer_apply_two_finger(r, 0.0, 0.0, 2.0, 100.0, 100.0, W, H);
    bed_mesh_renderer_set_render_mode(r, helix::BedMeshRenderMode::Force3D);
    CHECK(bed_mesh_renderer_get_view_state(r)->zoom == 1.0);

    bed_mesh_renderer_destroy(r);
}
#endif
```

(Check `bed_mesh_point_3d_t` field names `screen_x`/`screen_y` and that `helix::BedMeshRenderMode::Force3D` is the enum's spelling in `include/bed_mesh_renderer.h`; both appear in the current projection and renderer code.)

- [ ] **Step 2: Run to verify it fails**

Run: `make t F='[bed_mesh][zoom]'`
Expected: compile failure, `no member named 'zoom' in 'bed_mesh_view_state_t'`.

- [ ] **Step 3: Constants and view-state fields**

`include/bed_mesh_renderer.h`, beside `BED_MESH_DEFAULT_Z_SCALE`:

```cpp
#define BED_MESH_ZOOM_MIN 1.0 // Two-finger magnify range; 1.0 is the fitted view
#define BED_MESH_ZOOM_MAX 8.0
```

In `bed_mesh_view_state_t`, after `layer_offset_y`:

```cpp
    // Two-finger magnify, applied in screen space after the perspective divide.
    // zoom == BED_MESH_ZOOM_MIN implies pan_x == pan_y == 0.
    double zoom = 1.0;  // Magnification about the projection origin
    double pan_x = 0.0; // Screen-space offset, canvas pixels
    double pan_y = 0.0;
```

Declare beside `bed_mesh_renderer_set_rotation`:

```cpp
/**
 * Apply one frame of a two-finger gesture: pan by (pan_dx, pan_dy) canvas px,
 * then zoom by `zoom` about canvas-local (anchor_x, anchor_y).
 * Caller holds the render mutex in async mode.
 */
void bed_mesh_renderer_apply_two_finger(bed_mesh_renderer_t* renderer, double pan_dx,
                                        double pan_dy, double zoom, double anchor_x,
                                        double anchor_y, int canvas_width, int canvas_height);
```

- [ ] **Step 4: Projection and helpers**

`include/bed_mesh_projection.h`, beside the existing declaration:

```cpp
/// Pan the magnified view by (dx, dy) canvas px. No-op at BED_MESH_ZOOM_MIN.
void bed_mesh_projection_pan(bed_mesh_view_state_t* view, double dx, double dy);

/// Zoom by factor about canvas-local (anchor_x, anchor_y), clamped to
/// [BED_MESH_ZOOM_MIN, BED_MESH_ZOOM_MAX]. Reaching the minimum resets pan.
void bed_mesh_projection_zoom_at(bed_mesh_view_state_t* view, double factor, double anchor_x,
                                 double anchor_y, int canvas_width, int canvas_height);

/// Back to the fitted view: zoom 1, no pan.
void bed_mesh_projection_reset_zoom(bed_mesh_view_state_t* view);
```

`src/rendering/bed_mesh_projection.cpp`: add `#include <algorithm>` and `#include <cmath>`, and change Step 4 of the projection to

```cpp
    // Step 4: Perspective projection (similar triangles), then the two-finger magnify
    double perspective_x = (final_x * view->fov_scale) / final_z * view->zoom + view->pan_x;
    double perspective_y = (final_y * view->fov_scale) / final_z * view->zoom + view->pan_y;
```

Then, before `#endif`:

```cpp
void bed_mesh_projection_reset_zoom(bed_mesh_view_state_t* view) {
    view->zoom = BED_MESH_ZOOM_MIN;
    view->pan_x = 0.0;
    view->pan_y = 0.0;
}

void bed_mesh_projection_pan(bed_mesh_view_state_t* view, double dx, double dy) {
    if (view->zoom <= BED_MESH_ZOOM_MIN) {
        return;
    }
    view->pan_x += dx;
    view->pan_y += dy;
}

void bed_mesh_projection_zoom_at(bed_mesh_view_state_t* view, double factor, double anchor_x,
                                 double anchor_y, int canvas_width, int canvas_height) {
    if (!(factor > 0.0) || !std::isfinite(factor)) {
        return;
    }
    const double old_zoom = view->zoom;
    const double new_zoom = std::clamp(old_zoom * factor, BED_MESH_ZOOM_MIN, BED_MESH_ZOOM_MAX);
    if (new_zoom <= BED_MESH_ZOOM_MIN) {
        bed_mesh_projection_reset_zoom(view);
        return;
    }
    // The projection's own origin, so the anchored point maps back onto itself.
    const double ox = canvas_width / 2 + view->center_offset_x;
    const double oy = canvas_height * BED_MESH_Z_ORIGIN_VERTICAL_POS + view->center_offset_y;
    const double ratio = new_zoom / old_zoom;
    view->pan_x = (anchor_x - ox) - (anchor_x - ox - view->pan_x) * ratio;
    view->pan_y = (anchor_y - oy) - (anchor_y - oy - view->pan_y) * ratio;
    view->zoom = new_zoom;
}
```

- [ ] **Step 5: Renderer wiring and resets**

`src/rendering/bed_mesh_renderer.cpp`:

- In `bed_mesh_renderer_create`, beside the other view-state defaults: `bed_mesh_projection_reset_zoom(&renderer->view_state);`
- In `bed_mesh_renderer_set_bounds`, directly before `renderer->view_state.fov_scale = INITIAL_FOV_SCALE;`:

```cpp
    // The auto-fit that follows projects through the magnify, so it must see the fitted view.
    bed_mesh_projection_reset_zoom(&renderer->view_state);
```

- In `bed_mesh_renderer_set_render_mode`, after `renderer->render_mode = mode;`: `bed_mesh_projection_reset_zoom(&renderer->view_state);`
- New function after `bed_mesh_renderer_set_rotation`, invalidating the projection cache the same way `set_rotation` does:

```cpp
void bed_mesh_renderer_apply_two_finger(bed_mesh_renderer_t* renderer, double pan_dx,
                                        double pan_dy, double zoom, double anchor_x,
                                        double anchor_y, int canvas_width, int canvas_height) {
    if (!renderer) {
        return;
    }
    bed_mesh_projection_pan(&renderer->view_state, pan_dx, pan_dy);
    bed_mesh_projection_zoom_at(&renderer->view_state, zoom, anchor_x, anchor_y, canvas_width,
                                canvas_height);
    // Zoom and pan change every projected vertex (READY_TO_RENDER -> MESH_LOADED)
    if (renderer->state == RendererState::READY_TO_RENDER) {
        renderer->state = RendererState::MESH_LOADED;
    }
}
```

Confirm `bed_mesh_projection.h` is already included by `bed_mesh_renderer.cpp`; add it if not.

- [ ] **Step 6: Run the tests and the existing bed mesh tags**

Run: `make t F='[bed_mesh][zoom]'` → 8 cases pass.
Run: `./build/bin/helix-tests '[bed_mesh]' '~[slow]'` → green (rasterizer, canvas wiring).
Run: `./build/bin/helix-tests '[calibration][transform]'` → green.

- [ ] **Step 7: Commit**

```bash
git add include/bed_mesh_renderer.h include/bed_mesh_projection.h src/rendering/bed_mesh_projection.cpp src/rendering/bed_mesh_renderer.cpp tests/unit/test_bed_mesh_zoom_pan.cpp
git commit -m "feat(bed_mesh): screen-space zoom and pan in the 3D projection, reset by new bounds and render-mode changes"
git show --stat HEAD
```

---

### Task 6: Bed mesh widget gestures

**Files:**
- Modify: `src/ui/ui_bed_mesh.cpp`: `bed_mesh_widget_data_t` (~36-64), `bed_mesh_press_cb`, `bed_mesh_pressing_cb`, `bed_mesh_release_cb`, new `bed_mesh_gesture_cb`, event registration (~570-575).

**Interfaces:**
- Consumes: Task 1's `read_two_finger_sample`, `two_finger_step`, `TwoFingerState`, `kRotateDegreesPerPixel`; Task 5's `bed_mesh_renderer_apply_two_finger`.

No unit test, for the same reason as Task 4; covered by Task 8.

- [ ] **Step 1: Include, fields, constant**

Add `#include "view_gestures.h"`. In `bed_mesh_widget_data_t`, after `last_drag_pos`:

```cpp
    // Two-finger pan/zoom (3D only). two_finger_occurred holds one-finger
    // rotate off until every finger is up.
    helix::ui::TwoFingerState two_finger;
    bool two_finger_occurred = false;
```

(Check how the struct is allocated in `ui_bed_mesh_create`: it holds a `std::unique_ptr`, so it must be `new`-constructed and the initializers apply. If it is `lv_malloc`'d and memset, stop and report.)

In `bed_mesh_pressing_cb`, replace the literals:

```cpp
        const double delta_z = dx * helix::ui::kRotateDegreesPerPixel;
        const double delta_x = -dy * helix::ui::kRotateDegreesPerPixel; // Flip Y for intuitive tilt
```

and update the comment above them from "Scale factor: ~0.5 degrees per pixel (matching G-code viewer)" to "Shared rotation sensitivity with the G-code preview".

- [ ] **Step 2: Press, pressing, release**

In `bed_mesh_press_cb`, in the 3D path directly after `data->last_drag_pos = point;`:

```cpp
    data->two_finger = {};
    data->two_finger_occurred = false;
```

In `bed_mesh_pressing_cb`, after the missed-release safety block and before `lv_indev_get_point`:

```cpp
    if (data->two_finger_occurred) {
        return;
    }
```

In `bed_mesh_release_cb`, in the 3D path next to `data->is_dragging = false;`:

```cpp
    data->two_finger = {};
    data->two_finger_occurred = false;
```

- [ ] **Step 3: Gesture callback**

Add after `bed_mesh_release_cb`:

```cpp
#if LV_USE_GESTURE_RECOGNITION
// Two-finger pan and pinch zoom, 3D mode only.
static void bed_mesh_gesture_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    bed_mesh_widget_data_t* data = (bed_mesh_widget_data_t*)lv_obj_get_user_data(obj);

    if (!data || !data->renderer || bed_mesh_renderer_is_using_2d(data->renderer))
        return;

    const auto sample = helix::ui::read_two_finger_sample(e);
    if (!sample)
        return;

    const helix::ui::TwoFingerStep step = helix::ui::two_finger_step(*sample, data->two_finger);
    if (!step.active)
        return;
    data->two_finger_occurred = true;

    if (step.pan_dx == 0.0f && step.pan_dy == 0.0f && step.zoom == 1.0f)
        return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int width = lv_area_get_width(&coords);
    const int height = lv_area_get_height(&coords);
    const double anchor_x = step.anchor_x - coords.x1;
    const double anchor_y = step.anchor_y - coords.y1;
    auto apply = [&]() {
        bed_mesh_renderer_apply_two_finger(data->renderer, step.pan_dx, step.pan_dy, step.zoom,
                                           anchor_x, anchor_y, width, height);
    };

    if (data->async_mode && data->render_thread) {
        {
            std::lock_guard<std::mutex> lock(data->render_thread->render_mutex());
            apply();
        }
        data->render_thread->request_render();
    } else {
        apply();
    }
    lv_obj_invalidate(obj);
}
#endif
```

- [ ] **Step 4: Register it**

After the `LV_EVENT_PRESS_LOST` registration:

```cpp
#if LV_USE_GESTURE_RECOGNITION
    lv_obj_add_event_cb(obj, bed_mesh_gesture_cb, LV_EVENT_GESTURE, nullptr);
#endif
```

- [ ] **Step 5: Verify**

Run: `grep -n "\* 0\.5" src/ui/ui_bed_mesh.cpp` → no rotation literals left.
Run: `scripts/syntax_check.py src/ui/ui_bed_mesh.cpp` → clean.
Run: `make t F='[bed_mesh]'` → green.
Mock smoke (pinned socket, `ctl navigate` to the bed mesh panel): one-finger drag still rotates; switching to 2D and back works; no errors in the `-vv` log.

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui_bed_mesh.cpp
git commit -m "feat(bed_mesh): two-finger pan and anchored pinch zoom on the 3D mesh; rotate held off until all fingers lift"
git show --stat HEAD
```

---

### Task 6.5: Human test on the desk Pi 5

The first time real fingers touch the feature, before docs and the final gate, so anything that feels wrong goes back into Tasks 4-6 while they are fresh.

**Device:** Pi 5 at `192.168.1.113` (`pbrown@`, key auth), FocalTech ft5x06 capacitive DSI panel, the default `deploy-pi` target (`PI_HOST ?= 192.168.1.113`, `PI_DEPLOY_DIR ?= ~/helixscreen` in `mk/cross.mk`). Fallbacks if its panel turns out single-touch: the Pi 3B (`192.168.1.163`, password auth), then the Voron CB1 (`biqu@192.168.1.112`, BTT-HDMI5; it is also the only AFC rig, so ask before taking it).

- [ ] **Step 1: Ask Preston, claim, and confirm the panel is multi-touch**

Ask before the first command to the device. Then:

```bash
scripts/helix-claim check device:pi5
scripts/helix-claim take device:pi5 "two-finger gesture human test" --note "deploys branch build, --test mock"
ssh pbrown@192.168.1.113 "grep -i -B1 -A9 'ft5\|edt-ft\|touch' /proc/bus/input/devices"
```

Read the touch device's `B: ABS=` mask. Multi-touch axes are codes 0x2f-0x3d, so a multi-touch panel's mask has bits set above bit 32 (the mask prints as two words, e.g. `660800000000000 3`). A mask of just `3` (X and Y only) is single-touch: stop, release the claim, and move to the next fallback.

- [ ] **Step 2: Build and deploy the branch**

```bash
make pi-docker            # in the worktree; bind-mounts it at /src
make compile_commands     # pi-docker leaves /src paths in compile_commands.json
make deploy-pi
```

Check that the new binary is running and that the ctl server is compiled in:

```bash
ssh pbrown@192.168.1.113 "sudo journalctl -u helixscreen -n 30 --no-pager | grep -i 'version\|started'"
ssh pbrown@192.168.1.113 "strings -a ~/helixscreen/bin/helix-screen | grep -c list_callbacks"   # > 0
```

- [ ] **Step 3: Relaunch under the mock printer**

`--test` gives a bed mesh and G-code files with no printer attached and nothing that can move.

```bash
ssh pbrown@192.168.1.113 "sudo -n systemctl stop helixscreen"
ssh -f pbrown@192.168.1.113 "cd ~/helixscreen && setsid ./bin/helix-screen --test -vv --remote-socket /tmp/helix-gesture.sock > /tmp/helix-gesture.log 2>&1 < /dev/null"
ssh pbrown@192.168.1.113 "~/helixscreen/bin/helix-screen ctl -s /tmp/helix-gesture.sock current"
```

- [ ] **Step 4: Preston tests the G-code preview**

Drive to a file's 3D preview with `ctl` (`ctl ls` to find a mock file, then open its detail view), confirm with `ctl current`, then ask Preston to run the checks from Task 8 Step 3 on the preview: 1 (pinch into a corner), 2 (two-finger drag), 3 (pinch and move together), 4 (lift one finger mid-gesture: no rotate, no tap-select, no exclude prompt), 5 (one-finger rotate afterwards, no jump) and 7 (sharp again on release). Also check that a plain tap still selects an object and a one-second hold still opens the exclude flow.

- [ ] **Step 5: Preston tests the bed mesh**

`ctl navigate` to the bed mesh panel (3D view). Checks 1-6, including 6 (pinch fully out returns to the fitted view), and that one-finger drag rotates normally afterwards.

- [ ] **Step 6: Read the log, restore the Pi, record the result**

```bash
ssh pbrown@192.168.1.113 "grep -iE 'error|warn|GCode Viewer|bed_mesh' /tmp/helix-gesture.log | tail -40"
PID=$(ssh pbrown@192.168.1.113 "for p in \$(pgrep -x helix-screen); do grep -qz helix-gesture.sock /proc/\$p/cmdline && echo \$p; done")
ssh pbrown@192.168.1.113 "kill $PID; sudo -n systemctl start helixscreen"
scripts/helix-claim release device:pi5
```

Kill the PID resolved from the socket, never by name. Anything Preston flags goes back to Task 4 or 6 as a fix, followed by a re-run of this task. Write down what passed; the merge commit body cites it as the hardware evidence.

---

### Task 7: Docs

**Files:**
- Modify: `docs/user/guide/getting-started.md` (gesture table, ~43)
- Modify: whichever of `docs/user/guide/printing.md`, `print-monitoring.md`, `calibration.md` describe the preview or bed mesh gestures (`grep -niE "drag|rotate|pinch|zoom" docs/user/guide/{printing,print-monitoring,calibration}.md`)
- Modify: `docs/devel/GESTURE_RECOGNITION.md`
- Modify: `docs/devel/BED_MESH_RENDERING_INTERNALS.md` (~39)
- Modify: `docs/devel/architecture/16-gcode-pipeline.md` (gesture paragraph, ~167)
- Modify: `docs/devel/EXCLUDE_OBJECTS.md` (~260)
- Modify: `docs/devel/CLAUDE.md` (index line for GESTURE_RECOGNITION.md, ~57)

- [ ] **Step 1: User docs**

In `getting-started.md`, replace the pinch row with:

```markdown
| **Pinch/spread** | Zoom the 3D G-code preview or bed mesh in on the spot between your fingers |
| **Two-finger drag** | Move a zoomed 3D view around |
```

and add below the table:

```markdown
Pinch and two-finger drag need a screen that detects two fingers at once. Resistive screens (Flashforge Adventurer 5M and 5X, Creality Nebula Pad, Ender-3 V3 and V3 KE) detect one finger, so on those printers the 3D views rotate with a one-finger drag but do not zoom or pan. On the bed mesh, pinching all the way out returns to the full view.
```

Apply the same wording to any gesture description found by the grep above. User docs: no source paths (`docs/user/CLAUDE.md`).

- [ ] **Step 2: `GESTURE_RECOGNITION.md`**

Rewrite from research notes into the reference for how the app uses gestures. Retitle it `# Gesture Recognition: Pinch Zoom and Two-Finger Pan`. Keep the recognizer-order and PINCH-vs-ROTATE sections (they are still true) and update the threshold section to name `DisplayBackend::configure_touch_gestures` (`src/api/display_backend.cpp#DisplayBackend::configure_touch_gestures`) as the single place the thresholds are set, for DRM and fbdev. Add sections covering:

- Exclusivity: the first recognizer to reach RECOGNIZED owns the touch until a finger lifts; PINCH is checked before TWO_FINGERS_SWIPE.
- Pan comes from `info->delta_x/delta_y`, which PINCH also updates, read through `lv_indev_gesture_private.h` in `src/ui/view_gestures.cpp#read_two_finger_sample`, the only reader. The public swipe API is a distance plus a 4-way direction; `lv_indev_get_gesture_center_point` is the starting centre.
- `two_finger_step`: cumulative-to-per-frame conversion, first-frame catch-up, the (0.7, 1.4) frame filter, ONGOING and ENDED resets.
- Per-view mapping: `GCodeCamera::pan_pixels`/`zoom_at`; bed mesh `bed_mesh_projection_zoom_at`/`_pan`; pan first, then zoom about the live anchor.
- The latch: each widget holds rotate, tap and long-press off from the first two-finger frame until all fingers lift, cleared on PRESSED and RELEASED.
- Testing: desktop SDL is mouse-only, so gestures are verified on capacitive hardware.

Use `path#symbol` citations, never line numbers.

- [ ] **Step 3: Devel docs**

- `BED_MESH_RENDERING_INTERNALS.md`: change "Drag to rotate, pinch to zoom (future)" to "Drag to rotate; pinch to zoom and two-finger drag to pan (screen-space magnify, see Projection)". In the projection description, add that `zoom`/`pan_x`/`pan_y` apply after the perspective divide, and that `bed_mesh_renderer_set_bounds` and `bed_mesh_renderer_set_render_mode` reset them, because the auto-fit projects through them.
- `16-gcode-pipeline.md` and `EXCLUDE_OBJECTS.md`: in the tap definition, change "no pinch" to "no two-finger gesture", and add one sentence: a pinch zooms about the fingers and pans with them, a two-finger drag pans, and rotate, tap and long-press stay off from the first two-finger frame until every finger lifts.
- `docs/devel/CLAUDE.md`: index line becomes `| GESTURE_RECOGNITION.md | Pinch zoom and two-finger pan: LVGL recognizers, view_gestures, per-view mapping |`.

- [ ] **Step 4: Check and commit**

Run: `make check-doc-anchors` → no new unresolved anchors for the files touched.

```bash
git add docs/user/guide/getting-started.md docs/devel/GESTURE_RECOGNITION.md docs/devel/BED_MESH_RENDERING_INTERNALS.md docs/devel/architecture/16-gcode-pipeline.md docs/devel/EXCLUDE_OBJECTS.md docs/devel/CLAUDE.md
# plus any user guide page edited in Step 1, by name
git commit -m "docs: pinch zoom and two-finger pan on the 3D preview and bed mesh"
git show --stat HEAD
```

---

### Task 8: Verification and ship

- [ ] **Step 1: Full gate**

Run: `make full-test-run` (foreground; don't pipe through tail or head). Expected: unit sweep and bats green.

- [ ] **Step 2: Mutation check**

Run: `make mutate-diff`. Expected: the hunks in `view_gestures.cpp`, `gcode_camera.cpp`, `bed_mesh_projection.cpp` and `display_backend.cpp` go red when reverted. Note one killed mutation per behaviour for the merge commit body (for example: "dropping the ONGOING reset in two_finger_step fails 'ONGOING discards stale totals'").

- [ ] **Step 3: Hardware verification**

Task 6.5 covered the Pi 5 (DRM). This step covers the rest of the capacitive fleet. Ask Preston before the first device command; claim each device (`scripts/helix-claim take device:<name> ...`). Deploy the branch build to the devices available: a K1C or K2 Plus, the Qidi Q2 (fbdev, the only real check of the Task 2 threshold helper) and the Snapmaker U1. On each, reach the G-code preview (a loaded file) and the bed mesh 3D view with `ctl navigate`, `ctl current` first, then have Preston:

1. Pinch in on a corner: the corner stays under the fingers.
2. Two-finger drag: content follows the fingers.
3. Pinch and move at once: zoom and pan together.
4. Lift one finger mid-gesture and keep moving the other: no rotation, and no tap-select or exclude-object prompt.
5. Lift all, one-finger drag: rotates normally, no jump.
6. Bed mesh: pinch fully out returns to the fitted view.
7. G-code preview: after a pinch, the image returns to full sharpness on release.

`ctl navigate` back to where the screen was. For any device not reachable, file a `hw-verify` issue listing these seven checks.

- [ ] **Step 4: Delete the plan files and hand off for merge**

```bash
rm docs/devel/plans/2026-09-26-two-finger-pan-zoom-design.md docs/devel/plans/2026-09-26-two-finger-pan-zoom.md
git commit -m "docs(plans): two-finger pan/zoom shipped; remove plan scaffolding" -- docs/devel/plans/2026-09-26-two-finger-pan-zoom-design.md docs/devel/plans/2026-09-26-two-finger-pan-zoom.md
git show --stat HEAD
```

Then request the whole-branch review and merge per the finishing-a-development-branch skill; tear down the worktree with `scripts/teardown-worktree.sh two-finger-pan-zoom` after merge.

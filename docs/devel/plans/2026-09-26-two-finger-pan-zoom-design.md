# Two-finger pan and pinch zoom: G-code preview and bed mesh

Status: design, awaiting review. Promote to `docs/devel/plans/` when the worktree exists; delete in the change that ships the work.

## Goal

On capacitive (multi-touch) screens, let a user inspect part of a print preview or a bed mesh closely: pinch to zoom in on the spot under their fingers, drag with two fingers to move the zoomed view, and do both in one motion. One-finger rotate keeps working; tap and long-press on the G-code preview never fire by accident during or after a two-finger gesture.

Success: on a capacitive device, both views zoom anchored under the fingers, pan with the content staying under the fingers, and never register a stray rotate, tap, pick or exclude-object from a two-finger touch.

## Non-goals

- Single-touch (resistive) screens: AD5M, AD5X, Nebula Pad, Ender-3 V3 / V3 KE. They cannot report two fingers; they keep today's behaviour and get nothing new. No on-screen zoom buttons, no double-tap fallback.
- Desktop multi-touch. The SDL backend is mouse-only; no wheel zoom or modifier-drag is added. Behaviour is verified on hardware.
- G-code viewer 2D mode. Gestures stay 3D-only, as pinch is today.
- Sharing one-finger handling between the views. Tap-pick, long-press exclude, the ~30fps invalidate throttle, the bed mesh 2D tooltip and its async render path stay where they are.
- A shared camera abstraction. `GCodeCamera` (orthographic, glm, orbit target) and `bed_mesh_view_state_t` (perspective, doubles) stay separate.

## What LVGL 9.5 gives us

Read in `lib/lvgl/src/indev/lv_indev_gesture.c`:

- Recognizers are **exclusive**. `lv_indev_gesture_recognizers_update` evaluates them in enum order (PINCH before TWO_FINGERS_SWIPE); the first to reach RECOGNIZED owns the touch until a finger lifts, and every other recognizer is reset.
- PINCH recognizes when cumulative scale leaves `[pinch_down, pinch_up]` (DRM sets 0.85 / 1.15). TWO_FINGERS_SWIPE recognizes when the two-finger centre moves `gesture_min_distance` (50px default).
- Each recognizer owns its own `lv_indev_gesture_t info`. While PINCH is RECOGNIZED, `gesture_calculate_factors` keeps updating `info->delta_x/delta_y` (cumulative centre translation since the touch began) as well as `scale`. So a pinch carries pan data.
- The public API is not enough for pan: TWO_FINGERS_SWIPE exposes only a distance and a 4-way `lv_dir_t`, and `lv_indev_get_gesture_center_point` returns `info->center`, which is the **starting** centre, set once in `gesture_update_center_point`. Pan therefore reads `indev->recognizers[type].info` through `lv_indev_gesture_private.h`. Precedent: `src/ui/ui_utils.cpp` already includes `lv_indev_private.h` for a missing getter.
- evdev feeds real multi-touch into the recognizers; the calibration and frame-hook wrappers pass gesture data through.

## Interpretation rules

| First recognized | Result until a finger lifts |
|---|---|
| PINCH | Zoom by the scale change, anchored at the live finger centre, **and** pan by the centre's movement |
| TWO_FINGERS_SWIPE | Pan only. Zooming needs a lift and a fresh pinch. |

- Pan applies the *cumulative* translation, as a per-frame delta against the last frame. The first recognized frame therefore catches up the motion made before the threshold was met, so content ends up exactly under the fingers.
- Live anchor = starting centre + cumulative translation.
- Once any two-finger gesture is recognized, the view latches: one-finger rotate, tap and long-press are suppressed until **every** finger is up. Rotation already applied by the first finger before the second landed stays; it is a few degrees at most.

## Shared piece: `two_finger_step`

One small module, `include/view_gestures.h` + `src/ui/view_gestures.cpp`. The pure step uses its own plain types (no LVGL enums), so it tests without LVGL. It is the only code that reads LVGL's private recognizer struct, so an LVGL upgrade that reshapes it breaks one function rather than two views.

```cpp
namespace helix::ui {

inline constexpr float kRotateDegreesPerPixel = 0.5f;

struct TwoFingerSample {             // what LVGL reports this frame
    enum class Kind { Pinch, Pan } kind;
    enum class Phase { Ongoing, Recognized, Ended } phase;
    float delta_x, delta_y;          // cumulative centre translation, px
    float scale;                     // cumulative, 1.0 = unchanged; ignored for Pan
    int start_x, start_y;            // centre when the gesture began, screen px
};

struct TwoFingerState {              // owned by the view, one per widget
    float last_dx = 0, last_dy = 0, last_scale = 1;
};

struct TwoFingerStep {
    float pan_dx = 0, pan_dy = 0;    // px to move content this frame
    float zoom = 1;                  // multiplicative zoom this frame
    int anchor_x = 0, anchor_y = 0;  // live finger centre, screen px
    bool active = false;             // a gesture is in progress
    bool ended = false;              // gesture finished this frame
};

// Pure: no LVGL calls. Unit-tested directly.
TwoFingerStep two_finger_step(const TwoFingerSample& s, TwoFingerState& st);

#if LV_USE_GESTURE_RECOGNITION
// The only reader of LVGL's private gesture struct. nullopt when the event
// carries no pinch or two-finger swipe.
std::optional<TwoFingerSample> read_two_finger_sample(lv_event_t* e);
#endif

}
```

- `two_finger_step` keeps the existing per-frame sanity filter from `gcode_viewer_gesture_cb`: a frame scale ratio outside 0.7–1.4, or a non-positive scale, is treated as zoom 1.0 for that frame (pan still applies).
- ENDED or CANCELED resets `st` and returns `ended = true`.
- `kRotateDegreesPerPixel` replaces the two hand-kept 0.5 deg/px constants (`ROTATION_DEGREES_PER_PIXEL` in `src/ui/ui_gcode_viewer.cpp` and the literal in `src/ui/ui_bed_mesh.cpp#bed_mesh_pressing_cb`).

Each view keeps its own latch next to its existing tap, rotate and long-press state.

## G-code viewer

- New pure methods on `GCodeCamera` (`include/gcode_camera.h`, `src/rendering/gcode_camera.cpp`), built on the existing ones:
  - `pan_pixels(float dx, float dy)`: using the viewport size the camera already stores, converts screen px to world units from the orthographic view size at the current `zoom_level_`, then calls `pan()`. First production caller of `pan()`.
  - `zoom_at(float factor, float anchor_x, float anchor_y)` (widget-local px): applies `zoom()` (existing 0.1–100 clamp), then pans so the world point under `anchor` projects back to `anchor`. Uses the clamped effective factor.
- `gcode_viewer_gesture_cb` (`src/ui/ui_gcode_viewer.cpp`): rewritten around `read_two_finger_sample` + `two_finger_step`; handles PINCH and TWO_FINGERS_SWIPE; applies `pan_pixels` then `zoom_at` about the live anchor, so the point that was under the fingers follows them and stays put while scaling.
- Latch: the existing `is_pinching` / `pinch_occurred` / `last_pinch_scale` fields in `GCodeViewerState` become a `TwoFingerState` plus a "suppress until all fingers up" flag covering drag-rotate, tap-pick and the long-press timer. The long-press timer is cancelled when a two-finger gesture is recognized.
- One-finger rotate keeps orbiting `target_`, so after a pan it pivots around what the user is looking at. `reset()` and `fit_to_bounds()` already reset `target_`, so a new file or preset view clears pan.
- `gcode_viewer_release_cb` skips only the tap block after a two-finger gesture, so it still leaves interaction mode and draws the full-resolution final frame.

## Bed mesh

- `bed_mesh_view_state_t` (`include/bed_mesh_renderer.h`) gains `zoom` (1.0) and `pan_x`, `pan_y` (px, 0).
- The projection in `src/rendering/bed_mesh_projection.cpp` applies them after the perspective divide: `screen = canvas_centre + perspective * zoom + pan + existing offsets`. Every projected element (surface, grid, axis labels) goes through this function, so they stay consistent and the rasterizer is untouched. This is a screen-space magnify: one-finger rotate still orbits the mesh centre, so rotating while zoomed into a corner swings that corner across the screen. Accepted for now; a world-space pivot is the upgrade if it bothers anyone in practice.
- Anchored zoom in screen space: `pan' = anchor - centre - (anchor - centre - pan) * (zoom'/zoom)`.
- Zoom clamps to 1.0–8.0. Reaching 1.0 snaps pan to 0, so pinching all the way out is the reset; pan is a no-op at zoom 1.0. Zoom and pan also reset in `bed_mesh_renderer_set_bounds` (new bed or mesh bounds, which also re-runs the auto-fit, so auto-fit never sees a zoomed view) and in `bed_mesh_renderer_set_render_mode`.
- `src/ui/ui_bed_mesh.cpp` adds an `LV_EVENT_GESTURE` handler using the shared function, plus its own latch, which the existing press/pressing/release/press-lost callbacks consult. Updates take the render mutex, as rotate does. While a gesture is active the widget uses the fast solid-colour drag rendering, restoring the gradient on release, as drag does today.

## Input backends

- The pinch/rotate threshold block in `src/api/display_backend_drm.cpp` (pinch up 1.15, down 0.85, rotation 3.14 rad) moves into one helper, called from both DRM branches and from `src/api/display_backend_fbdev.cpp`. fbdev currently runs LVGL's defaults (1.5 / 0.75 / 0.2 rad), where ROTATE wins the race; this affects the existing G-code pinch too.

## Docs

- `docs/user/guide/getting-started.md`: gesture table gains two-finger drag = pan; note that pinch and two-finger drag need a capacitive screen and do not work on resistive ones (AD5M, AD5X, Nebula Pad, Ender-3 V3).
- User pages covering the print preview and bed mesh (`printing.md`, `print-monitoring.md`, `calibration.md`): add or correct gesture wording where they describe those views.
- `docs/devel/GESTURE_RECOGNITION.md`: from pinch research notes to the reference for how the app uses gestures: recognizer exclusivity, pan via the private `info` fields, `two_finger_step`, the backend threshold helper.
- `docs/devel/BED_MESH_RENDERING_INTERNALS.md`: drop "pinch to zoom (future)", document zoom/pan in the projection step.
- `docs/devel/architecture/16-gcode-pipeline.md`, `docs/devel/EXCLUDE_OBJECTS.md`: gesture paragraphs gain two-finger pan and the all-fingers-up latch.
- `docs/devel/CLAUDE.md`: update the GESTURE_RECOGNITION.md index line.

## Testing

Test-first, in a worktree from `scripts/setup-worktree.sh`.

- `[gesture]` on `two_finger_step`: pan-only swipe; pinch with simultaneous pan; catch-up of pre-recognition motion on the first frame; per-frame deltas summing to the cumulative total; anchor = start centre + translation; implausible frame scale (outside 0.7–1.4, zero, negative) gives zoom 1.0 but still pans; ENDED and CANCELED reset state.
- `[gcode][camera]` on `GCodeCamera`: after `pan_pixels(dx, dy)` a projected world point moves by (dx, dy) ± 1px at two zoom levels; after `zoom_at`, the world point under the anchor projects back to the anchor; at the zoom clamp, the anchor still holds.
- `[bed_mesh]` on the projection: default zoom/pan gives output identical to current; zoom and pan applied as specified; anchored zoom keeps the anchor fixed; zoom-to-1.0 snaps pan to 0; clamp at 8.0.
- Latches stay in the widget callbacks and are covered by hardware verification, not unit tests.
- `make mutate-diff` over the change; the commit body names the mutation that proves the tests can fail.

## Hardware verification

Desktop cannot generate multi-touch. Verify on capacitive devices, claiming each with `scripts/helix-claim` and asking before first use:

- A K1 or K2 (DRM or fbdev per model), the Qidi Q2 (fbdev: proves the threshold helper), and the Snapmaker U1.
- Per device, driven to each view with `ctl`: pinch in on a corner and confirm it stays under the fingers; two-finger pan; pinch and pan in one motion; lift one finger mid-gesture and confirm no rotate, tap-pick or exclude-object fires; single-finger rotate afterwards; bed mesh pinch fully out resets.
- Any device not reachable gets a `hw-verify` issue.

## Risks

- `lv_indev_gesture_private.h` is not API. Mitigated by keeping every read in `read_two_finger_sample`.
- The 50px swipe threshold is shared with LVGL's one-finger gesture detection (`gesture_min_distance`), so it is not changed. If pan feels sticky to start, a lower value would need a per-indev decision.
- Some capacitive digitizers (Q2 over-reports its ABS range) may give noisy two-finger centres; the per-frame scale filter covers zoom, and pan noise would show up in hardware verification.

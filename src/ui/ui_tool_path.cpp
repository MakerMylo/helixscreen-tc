// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_tool_path.h"

#include "ui_filament_path_internal.h"
#include "ui_observer_guard.h"

#include "ams_state.h"
#include "ams_types.h"
#include "filament_sensor_manager.h"
#include "filament_tube_stroker.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "helix-xml/src/xml/lv_xml_parser.h"
#include "helix-xml/src/xml/lv_xml_widget.h"
#include "helix-xml/src/xml/parsers/lv_xml_obj_parser.h"
#include "observer_factory.h"
#include "theme_manager.h"
#include "tool_config.h"
#include "tool_filament_sensors.h"
#include "tool_state.h"
#include "toolchanger_vars.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

namespace helix::ui {

namespace {

enum class Mode { Vertical, Horizontal, Swatch, Hue };

struct ToolPathData {
    int tool_index = 0;
    Mode mode = Mode::Vertical;
    /// Non-null when the index follows a subject (the actions overlay).
    lv_subject_t* tool_subject = nullptr;

    // Every input the picture depends on; any change repaints.
    ObserverGuard color_observer;
    ObserverGuard status_observer;
    ObserverGuard tools_observer;
    ObserverGuard vars_observer;
    ObserverGuard sensors_observer;
    ObserverGuard config_observer;
    ObserverGuard tool_subject_observer;
};

std::unordered_map<lv_obj_t*, ToolPathData*> s_registry;

ToolPathData* get_data(lv_obj_t* obj) {
    auto it = s_registry.find(obj);
    return it == s_registry.end() ? nullptr : it->second;
}

/// What the picture needs to know, resolved from the singletons at draw time.
struct Snapshot {
    bool slot_has_color = false;
    lv_color_t filament{};
    bool mounted = false; ///< on the carriage (ToolState active)
    bool tool_exists = false;
    lv_color_t body{};        ///< the toolhead's own colour (ToolConfig)
    bool at_entry = false;    ///< filament in the bowden / entry segment
    bool at_toolhead = false; ///< filament through the gears to the nozzle
    bool has_entry_sensor = false;
    bool has_toolhead_sensor = false;
    bool entry_detected = false;
    bool toolhead_detected = false;
};

Snapshot take_snapshot(int tool) {
    Snapshot s;
    if (tool < 0) {
        return s;
    }
    s.body = lv_color_hex(helix::ToolConfig::instance().color(tool));

    // Tool identity
    auto& state = helix::ToolState::instance();
    const auto& tools = state.tools();
    if (tool < static_cast<int>(tools.size())) {
        s.tool_exists = true;
        s.mounted = state.active_tool_index() == tool || tools[static_cast<size_t>(tool)].active;
    }

    // Slot colour and status: on a tool changer slot == tool.
    bool slot_loaded = false;
    if (auto* backend = AmsState::instance().get_backend()) {
        const SlotInfo info = backend->get_slot_info(tool);
        if (info.slot_index >= 0 && info.has_filament_info()) {
            s.slot_has_color = true;
            s.filament = lv_color_hex(info.color_rgb);
        }
        slot_loaded = info.status == SlotStatus::LOADED;
    }

    // Memory first, the slot's own status as the fallback.
    const int memory = helix::ToolchangerVars::instance().loaded_for(tool);
    const bool remembered = memory >= 0 ? memory == 1 : slot_loaded;

    // Sensors override memory for the segment they watch.
    const auto pips = helix::tool_sensors::read_pips(tool);
    s.has_entry_sensor = pips.has_entry_sensor;
    s.has_toolhead_sensor = pips.has_toolhead_sensor;
    s.entry_detected = pips.entry.value_or(false);
    s.toolhead_detected = pips.toolhead.value_or(false);

    s.at_toolhead = pips.toolhead ? *pips.toolhead : remembered;
    if (pips.entry) {
        s.at_entry = *pips.entry;
    } else {
        // No entry sensor: filament reaching the toolhead has passed the entry.
        s.at_entry = s.at_toolhead || remembered;
    }
    return s;
}

struct Palette {
    lv_color_t idle;
    lv_color_t bg;
    lv_color_t accent;
    lv_color_t muted;
};

Palette palette() {
    const bool dark = theme_manager_is_dark_mode();
    Palette p;
    p.idle = theme_manager_get_color(dark ? "filament_idle_dark" : "filament_idle_light");
    p.bg = theme_manager_get_color("card_bg");
    p.accent = theme_manager_get_color("primary");
    p.muted = theme_manager_get_color("text_muted");
    return p;
}

/// The toolhead's own colour as a block: the column's header bar and the
/// settings rows. Nothing about the filament.
void draw_hue(lv_layer_t* layer, const lv_area_t& a, const Snapshot& s) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_MAX(2, lv_area_get_height(&a) / 4);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = s.body;
    dsc.border_width = 1;
    dsc.border_color = fpath::ph_darken(s.body, 40);
    dsc.border_opa = LV_OPA_COVER;
    lv_area_t box = a;
    lv_draw_rect(layer, &dsc, &box);
}

void draw_swatch(lv_layer_t* layer, const lv_area_t& a, const Snapshot& s, const Palette& p) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = LV_MAX(4, lv_area_get_height(&a) / 4);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = s.slot_has_color ? s.filament : p.idle;
    dsc.border_width = 1;
    dsc.border_color = fpath::ph_darken(dsc.bg_color, 40);
    dsc.border_opa = LV_OPA_COVER;
    lv_area_t box = a;
    lv_draw_rect(layer, &dsc, &box);
}

/// The sensor pips and segment colouring, shared by both orientations. The
/// path runs from `from` to `to` along one axis; the toolhead glyph sits at
/// `to`. Sensor positions are fractions of the run.
struct Run {
    bool horizontal;
    int32_t fixed; ///< x for vertical, y for horizontal
    int32_t from;  ///< start coordinate along the run (bowden end)
    int32_t to;    ///< end coordinate (toolhead top / left)
};

void draw_tube(lv_layer_t* layer, const Run& r, int32_t a, int32_t b, const LaneStyle& st) {
    if (r.horizontal) {
        draw_lane_hline(layer, a, b, r.fixed, st);
    } else {
        draw_lane_vline(layer, r.fixed, a, b, st);
    }
}

void draw_pip(lv_layer_t* layer, const Run& r, int32_t at, lv_color_t color, bool filled,
              int32_t radius) {
    if (r.horizontal) {
        fpath::draw_sensor_dot(layer, at, r.fixed, color, filled, radius);
    } else {
        fpath::draw_sensor_dot(layer, r.fixed, at, color, filled, radius);
    }
}

void draw_path(lv_layer_t* layer, const lv_area_t& a, const Snapshot& s, const Palette& p,
               Mode mode) {
    const int32_t w = lv_area_get_width(&a);
    const int32_t h = lv_area_get_height(&a);
    const bool horizontal = mode == Mode::Horizontal;

    // Sizes scale with the box: the glyph is roughly 4*scale wide and
    // 6.5*scale tall (top of the block to the nozzle tip), the tube a fraction
    // of the glyph.
    // Horizontal (the actions overlay) has a whole column to itself, so it
    // may grow larger than a column glyph; it is bounded by both axes.
    // Either orientation may have a whole column to itself (the actions
    // overlay), so both grow past a panel column's glyph; both are bounded by
    // the axis the drawing runs along as well as the one it sits across.
    const int32_t scale = horizontal ? LV_CLAMP(LV_MIN(h / 8, w / 10), 6, 40)
                                     : LV_CLAMP(LV_MIN(w / 7, h / 16), 6, 40);
    const int32_t line_w = LV_CLAMP(scale / 2, 3, 10);
    const int32_t pip_r = LV_CLAMP(scale / 2 + 1, 4, 9);

    const lv_color_t fil = s.slot_has_color ? s.filament : p.muted;

    // Glyph placement. Vertical: centred, in the lower part of the column so
    // the bowden run above it is the long one. Horizontal: to the right, its
    // block centred on the tube so the tube reads as entering its side.
    int32_t glyph_cx; // glyph centre x
    int32_t glyph_cy; // glyph cy (the canvas's toolhead_y: block top is cy - 2*scale)
    Run run;
    run.horizontal = horizontal;
    if (horizontal) {
        // Centre the drawing (block top to a short extrudate) in the box.
        const int32_t drawing_h = scale * 2 + (fpath::toolhead_tip_y(0, scale) + scale * 2);
        glyph_cy = a.y1 + LV_MAX(4 + scale * 2, (h - drawing_h) / 2 + scale * 2);
        glyph_cx = a.x1 + w * 3 / 4;
        run.fixed = glyph_cy + scale; // the block's mid-height
        run.from = a.x1 + pip_r;
        run.to = glyph_cx - scale * 2; // the block's left edge
    } else {
        glyph_cx = a.x1 + w / 2;
        glyph_cy = a.y1 + h * 62 / 100;
        run.fixed = glyph_cx;
        run.from = a.y1 + pip_r;
        run.to = glyph_cy - scale * 2; // the block's top
    }
    const int32_t span = run.to - run.from;
    if (span < scale * 3) {
        return; // too small to say anything
    }
    const int32_t entry_at = run.from + span * 35 / 100;
    const int32_t toolhead_at = run.from + span * 82 / 100;

    // Three runs of tube - before the entry pip, between the pips, after the
    // toolhead pip - with a pip drawn only where the printer has a sensor. A
    // run that meets the next with no pip between and the same colour is
    // drawn as one stroke, so no cap shows at the join.
    struct Seg {
        int32_t a, b;
        bool filled;
    };
    Seg segs[3] = {
        {run.from, s.has_entry_sensor ? entry_at - pip_r : entry_at, s.at_entry},
        {s.has_entry_sensor ? entry_at + pip_r : entry_at,
         s.has_toolhead_sensor ? toolhead_at - pip_r : toolhead_at, s.at_entry},
        {s.has_toolhead_sensor ? toolhead_at + pip_r : toolhead_at, run.to, s.at_toolhead},
    };
    const bool pip_after[3] = {s.has_entry_sensor, s.has_toolhead_sensor, false};
    int i = 0;
    while (i < 3) {
        int j = i;
        while (j + 1 < 3 && !pip_after[j] && segs[j + 1].filled == segs[i].filled) {
            ++j;
        }
        LaneStyle st = lane_style(segs[i].filled, fil, p.idle, p.bg, line_w);
        draw_tube(layer, run, segs[i].a, segs[j].b, st);
        if (pip_after[j]) {
            const bool on = j == 0 ? s.entry_detected : s.toolhead_detected;
            draw_pip(layer, run, j == 0 ? entry_at : toolhead_at, on ? fil : p.idle, on, pip_r);
        }
        i = j + 1;
    }

    // The toolhead's own colour (Settings > Tool Changer) is a plate behind
    // the glyph: the renderers draw every toolhead style in metallic grey and
    // tint only the nozzle tip, and the tip belongs to the filament. Docked
    // tools are dimmed as the AMS canvas dims them.
    {
        const int32_t half_w =
            scale * 29 / 10; // the glyph body is ~2.4*scale half-wide with its side
        lv_draw_rect_dsc_t plate;
        lv_draw_rect_dsc_init(&plate);
        plate.radius = LV_MAX(3, scale / 2);
        plate.bg_opa = s.mounted ? LV_OPA_COVER : LV_OPA_50;
        plate.bg_color = s.mounted ? s.body : fpath::ph_darken(s.body, 30);
        plate.border_width = 1;
        plate.border_color = fpath::ph_darken(s.body, 50);
        plate.border_opa = plate.bg_opa;
        lv_area_t box = {glyph_cx - half_w, glyph_cy - scale * 3, glyph_cx + half_w,
                         glyph_cy + scale * 23 / 10};
        lv_draw_rect(layer, &plate, &box);
    }
    const lv_color_t tip = s.at_toolhead ? fil : lv_color_hex(fpath::NOZZLE_UNLOADED_COLOR);
    const lv_opa_t opa = s.mounted ? LV_OPA_COVER : LV_OPA_60;
    fpath::draw_toolhead(layer, glyph_cx, glyph_cy, tip, scale, opa);

    // Below the nozzle tip: the extrudate run, coloured when filament is at
    // the toolhead.
    const int32_t tip_y = fpath::toolhead_tip_y(glyph_cy, scale);
    const int32_t bottom = horizontal ? LV_MIN(a.y2 - 2, tip_y + 2 + scale * 2) : a.y2 - pip_r;
    if (bottom > tip_y + 4) {
        LaneStyle st = lane_style(s.at_toolhead, fil, p.idle, p.bg, LV_MAX(2, line_w - 1));
        draw_lane_vline(layer, glyph_cx, tip_y + 2, bottom, st);
    }
}

void tool_path_draw_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto* data = get_data(obj);
    if (!data) {
        return;
    }
    lv_layer_t* layer = lv_event_get_layer(e);
    if (!layer) {
        return;
    }
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const Snapshot s = take_snapshot(data->tool_index);
    const Palette p = palette();
    if (data->mode == Mode::Swatch) {
        draw_swatch(layer, coords, s, p);
    } else if (data->mode == Mode::Hue) {
        draw_hue(layer, coords, s);
    } else {
        draw_path(layer, coords, s, p, data->mode);
    }
}

void invalidate(lv_obj_t* obj) {
    if (obj) {
        lv_obj_invalidate(obj);
    }
}

void setup_observers(lv_obj_t* obj, ToolPathData* data) {
    data->color_observer.reset();
    data->status_observer.reset();
    data->tools_observer.reset();
    data->vars_observer.reset();
    data->sensors_observer.reset();
    data->config_observer.reset();

    const int slot = data->tool_index;
    auto repaint = [](lv_obj_t* o, int /*value*/) { invalidate(o); };

    AmsState& ams = AmsState::instance();
    if (slot >= 0 && slot < AmsState::MAX_SLOTS) {
        data->color_observer = observe_int_sync<lv_obj_t>(ams.get_slot_color_subject(slot), obj,
                                                          repaint, ams.get_subjects_lifetime());
        data->status_observer = observe_int_sync<lv_obj_t>(ams.get_slot_status_subject(slot), obj,
                                                           repaint, ams.get_subjects_lifetime());
    }
    auto& tools = helix::ToolState::instance();
    data->tools_observer = observe_int_sync<lv_obj_t>(tools.get_tools_version_subject(), obj,
                                                      repaint, tools.get_subjects_lifetime());
    auto& vars = helix::ToolchangerVars::instance();
    data->vars_observer = observe_int_sync<lv_obj_t>(vars.get_version_subject(), obj, repaint,
                                                     vars.get_subjects_lifetime());
    auto& sensors = helix::FilamentSensorManager::instance();
    data->sensors_observer = observe_int_sync<lv_obj_t>(sensors.get_states_version_subject(), obj,
                                                        repaint, sensors.get_subjects_lifetime());
    auto& config = helix::ToolConfig::instance();
    data->config_observer = observe_int_sync<lv_obj_t>(config.get_version_subject(), obj, repaint,
                                                       config.get_subjects_lifetime());
}

void follow_tool_subject(lv_obj_t* obj, ToolPathData* data, const char* subject_name) {
    data->tool_subject_observer.reset();
    data->tool_subject = lv_xml_get_subject(nullptr, subject_name);
    if (!data->tool_subject) {
        spdlog::warn("[ToolPath] tool_subject '{}' not found", subject_name);
        return;
    }
    data->tool_index = lv_subject_get_int(data->tool_subject);
    setup_observers(obj, data);
    // The subject belongs to whichever panel registered it; observe_int_sync
    // needs a lifetime, and a panel subject outlives every widget bound to it
    // within the same screen, so the widget's own deletion is the guard here.
    data->tool_subject_observer = observe_int_sync<lv_obj_t>(
        data->tool_subject, obj,
        [](lv_obj_t* o, int index) {
            auto* d = get_data(o);
            if (!d || d->tool_index == index) {
                return;
            }
            d->tool_index = index;
            setup_observers(o, d);
            invalidate(o);
        },
        SubjectLifetime{});
}

void tool_path_delete_cb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto it = s_registry.find(obj);
    if (it != s_registry.end()) {
        std::unique_ptr<ToolPathData> data(it->second);
        s_registry.erase(it);
    }
}

void* tool_path_xml_create(lv_xml_parser_state_t* state, const char** attrs) {
    LV_UNUSED(attrs);
    void* parent = lv_xml_state_get_parent(state);
    lv_obj_t* obj = lv_obj_create(static_cast<lv_obj_t*>(parent));
    if (!obj) {
        return nullptr;
    }
    auto data = std::make_unique<ToolPathData>();
    s_registry[obj] = data.get();
    data.release();

    // DECLARATIVE_OK: a draw-hook widget owns its own canvas surface; the
    // transparent, borderless box is the widget, not styling of a child.
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, tool_path_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(obj, tool_path_delete_cb, LV_EVENT_DELETE, nullptr);
    return obj;
}

void tool_path_xml_apply(lv_xml_parser_state_t* state, const char** attrs) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_xml_state_get_item(state));
    if (!obj) {
        return;
    }
    lv_xml_obj_apply(state, attrs);
    auto* data = get_data(obj);
    if (!data) {
        return;
    }
    const char* subject_name = nullptr;
    for (int i = 0; attrs[i]; i += 2) {
        const char* name = attrs[i];
        const char* value = attrs[i + 1];
        if (std::strcmp(name, "tool_index") == 0) {
            data->tool_index = std::atoi(value);
        } else if (std::strcmp(name, "tool_subject") == 0) {
            subject_name = value;
        } else if (std::strcmp(name, "mode") == 0) {
            if (std::strcmp(value, "horizontal") == 0) {
                data->mode = Mode::Horizontal;
            } else if (std::strcmp(value, "swatch") == 0) {
                data->mode = Mode::Swatch;
            } else if (std::strcmp(value, "hue") == 0) {
                data->mode = Mode::Hue;
            } else {
                data->mode = Mode::Vertical;
            }
        }
    }
    if (subject_name) {
        follow_tool_subject(obj, data, subject_name);
    } else {
        setup_observers(obj, data);
    }
    lv_obj_invalidate(obj);
}

} // namespace

void ui_tool_path_register() {
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    lv_xml_register_widget("tool_path", tool_path_xml_create, tool_path_xml_apply);
    spdlog::debug("[ToolPath] Registered tool_path widget");
}

void ui_tool_path_set_tool(lv_obj_t* obj, int tool_index) {
    auto* data = get_data(obj);
    if (!data || data->tool_index == tool_index) {
        return;
    }
    data->tool_index = tool_index;
    setup_observers(obj, data);
    lv_obj_invalidate(obj);
}

} // namespace helix::ui

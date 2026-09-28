// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui_settings_tools.h"

#include "ui_event_safety.h"
#include "ui_nav_manager.h"
#include "ui_utils.h"

#include "display_numbering.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "static_panel_registry.h"
#include "tool_config.h"
#include "tool_state.h"

#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace helix::settings {

// ============================================================================
// SINGLETON
// ============================================================================

namespace {
std::unique_ptr<ToolsSettingsOverlay> g_overlay;
}

ToolsSettingsOverlay& get_tools_settings_overlay() {
    if (!g_overlay) {
        g_overlay = std::make_unique<ToolsSettingsOverlay>();
        StaticPanelRegistry::instance().register_destroy("ToolsSettingsOverlay",
                                                         []() { g_overlay.reset(); });
    }
    return *g_overlay;
}

// ============================================================================
// LIFECYCLE
// ============================================================================

ToolsSettingsOverlay::ToolsSettingsOverlay() {
    spdlog::debug("[{}] Created", get_name());
}

ToolsSettingsOverlay::~ToolsSettingsOverlay() {
    color_picker_.reset();
    deinit_subjects_base(subjects_);
}

void ToolsSettingsOverlay::init_subjects() {
    init_subjects_guarded([this]() {
        UI_MANAGED_SUBJECT_STRING(detected_, detected_buf_, "", "tools_cfg_detected", subjects_);
    });
}

void ToolsSettingsOverlay::register_callbacks() {
    lv_xml_register_event_cb(nullptr, "on_tools_cfg_count_changed", on_count_changed);
}

lv_obj_t* ToolsSettingsOverlay::create(lv_obj_t* parent) {
    if (overlay_root_) {
        return overlay_root_;
    }
    if (!create_overlay_from_xml(parent, "tools_settings_overlay")) {
        spdlog::error("[{}] Failed to create overlay from XML", get_name());
        return nullptr;
    }
    colors_list_ = lv_obj_find_by_name(overlay_root_, "tools_cfg_colors_list");
    lv_obj_add_flag(overlay_root_, LV_OBJ_FLAG_HIDDEN);
    return overlay_root_;
}

void ToolsSettingsOverlay::show(lv_obj_t* parent_screen) {
    parent_screen_ = parent_screen;
    if (!subjects_initialized_) {
        init_subjects();
        register_callbacks();
    }
    if (!overlay_root_ && parent_screen_) {
        create(parent_screen_);
    }
    if (!overlay_root_) {
        return;
    }
    NavigationManager::instance().register_overlay_instance(overlay_root_, this);
    populate();
    NavigationManager::instance().push_overlay(overlay_root_);
}

void ToolsSettingsOverlay::on_activate() {
    OverlayBase::on_activate();
    populate();
}

void ToolsSettingsOverlay::on_deactivating(DeactivateReason) {
    picking_tool_ = -1;
}

// ============================================================================
// CONTENT
// ============================================================================

int ToolsSettingsOverlay::shown_tool_count() const {
    const int detected = static_cast<int>(helix::ToolState::instance().tools().size());
    return helix::ToolConfig::instance().effective_tool_count(detected);
}

void ToolsSettingsOverlay::populate() {
    if (!overlay_root_) {
        return;
    }
    populate_count_dropdown();
    populate_colors();
}

void ToolsSettingsOverlay::populate_count_dropdown() {
    lv_obj_t* row = lv_obj_find_by_name(overlay_root_, "row_tools_cfg_count");
    lv_obj_t* dropdown = row ? lv_obj_find_by_name(row, "dropdown") : nullptr;
    if (!dropdown) {
        return;
    }
    std::string options = lv_tr("Auto");
    for (int n = 1; n <= helix::ToolConfig::kMaxTools; ++n) {
        options += fmt::format("\n{}", n);
    }
    lv_dropdown_set_options(dropdown, options.c_str());
    lv_dropdown_set_selected(dropdown,
                             static_cast<uint32_t>(helix::ToolConfig::instance().tool_count()));

    const int detected = static_cast<int>(helix::ToolState::instance().tools().size());
    const std::string line =
        detected > 0 ? fmt::format(fmt::runtime(lv_tr("Klipper reports {} tools")), detected)
                     : lv_tr("No tools reported yet");
    lv_subject_copy_string(&detected_, line.c_str());
}

void ToolsSettingsOverlay::populate_colors() {
    if (!colors_list_) {
        return;
    }
    const uint32_t child_count = lv_obj_get_child_count(colors_list_);
    for (int i = static_cast<int>(child_count) - 1; i >= 0; i--) {
        lv_obj_t* child = lv_obj_get_child(colors_list_, i);
        helix::ui::safe_delete(child);
    }

    auto& config = helix::ToolConfig::instance();
    const auto& tools = helix::ToolState::instance().tools();
    const int count = shown_tool_count();
    for (int i = 0; i < count; ++i) {
        const std::string name = i < static_cast<int>(tools.size())
                                     ? tools[static_cast<size_t>(i)].name
                                     : helix::ui::tool_label(i);
        std::string color_name = config.color_name(i);
        if (!config.has_custom_color(i)) {
            color_name = fmt::format("{} ({})", color_name, lv_tr("default"));
        }
        const std::string index = std::to_string(i);
        const std::string row_name = "tools_cfg_row_" + index;
        const char* attrs[] = {"name",        row_name.c_str(),   "tool_index",
                               index.c_str(), "tool_name",        name.c_str(),
                               "color_name",  color_name.c_str(), nullptr};
        auto* row =
            static_cast<lv_obj_t*>(lv_xml_create(colors_list_, "tools_settings_row", attrs));
        if (!row) {
            continue;
        }
        lv_obj_set_style_opa(row, LV_OPA_70, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(
            row,
            [](lv_event_t* e) {
                LVGL_SAFE_EVENT_CB_BEGIN("[ToolsSettings] row clicked");
                get_tools_settings_overlay().handle_color_clicked(
                    static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
                LVGL_SAFE_EVENT_CB_END();
            },
            LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        if (lv_obj_t* reset = lv_obj_find_by_name(row, "reset_button")) {
            if (!config.has_custom_color(i)) {
                lv_obj_add_state(reset, LV_STATE_DISABLED);
            }
            lv_obj_add_event_cb(
                reset,
                [](lv_event_t* e) {
                    LVGL_SAFE_EVENT_CB_BEGIN("[ToolsSettings] reset clicked");
                    get_tools_settings_overlay().handle_color_reset(
                        static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
                    LVGL_SAFE_EVENT_CB_END();
                },
                LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        }
    }
}

// ============================================================================
// EDITS
// ============================================================================

void ToolsSettingsOverlay::handle_count_changed(int index) {
    if (index < 0) {
        return;
    }
    helix::ToolConfig::instance().set_tool_count(index); // 0 = Auto
    populate();
}

void ToolsSettingsOverlay::handle_color_clicked(int tool) {
    if (!parent_screen_ || tool < 0) {
        return;
    }
    if (!color_picker_) {
        color_picker_ = std::make_unique<helix::ui::ColorPicker>();
    }
    picking_tool_ = tool;
    color_picker_->set_color_callback([](uint32_t rgb, const std::string& /*name*/) {
        auto& self = get_tools_settings_overlay();
        if (self.picking_tool_ >= 0) {
            helix::ToolConfig::instance().set_color(self.picking_tool_, rgb);
            self.picking_tool_ = -1;
            self.populate_colors();
        }
    });
    color_picker_->set_dismiss_callback([]() {
        // A cancelled pick leaves the colour alone.
        get_tools_settings_overlay().picking_tool_ = -1;
    });
    color_picker_->show_with_color(parent_screen_, helix::ToolConfig::instance().color(tool));
}

void ToolsSettingsOverlay::handle_color_reset(int tool) {
    helix::ToolConfig::instance().reset_color(tool);
    populate_colors();
}

// ============================================================================
// XML CALLBACKS
// ============================================================================

void ToolsSettingsOverlay::on_count_changed(lv_event_t* e) {
    LVGL_SAFE_EVENT_CB_BEGIN("[ToolsSettings] count changed");
    auto* dropdown = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (dropdown) {
        get_tools_settings_overlay().handle_count_changed(
            static_cast<int>(lv_dropdown_get_selected(dropdown)));
    }
    LVGL_SAFE_EVENT_CB_END();
}

} // namespace helix::settings

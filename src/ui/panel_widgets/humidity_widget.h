// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_widget_ref.h"

#include "panel_widget.h"

namespace helix {

/// Minimal widget class for humidity sensor — provides size-responsive scaling.
class HumidityWidget : public PanelWidget {
  public:
    HumidityWidget() = default;
    ~HumidityWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    void on_size_changed(int colspan, int rowspan, int width_px, int height_px) override;
    const char* id() const override {
        return "humidity";
    }

  private:
    helix::ui::WidgetRef widget_obj_;
};

} // namespace helix

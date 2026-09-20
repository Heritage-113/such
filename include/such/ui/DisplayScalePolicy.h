#pragma once

namespace such::ui {

struct DisplayScaleInput {
    float dpi = 0.0f;
    int pixel_width = 0;
    int pixel_height = 0;
    float backing_scale = 1.0f;
};

// X11 frequently reports a synthetic 96-DPI physical size under desktop
// scaling. Do not trust DPI alone: enforce a resolution-derived floor so 3K/4K
// desktops retain a usable physical control size.
float recommended_x11_ui_scale(const DisplayScaleInput& input) noexcept;


} // namespace such::ui

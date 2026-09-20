#include <such/ui/DisplayScalePolicy.h>

#include <algorithm>
#include <cmath>

namespace such::ui {
namespace {
float resolution_floor(int w, int h) noexcept {
    const int long_edge = std::max(w, h);
    const int short_edge = std::min(w, h);
    if (long_edge >= 3600 || short_edge >= 2000) return 2.40f;
    if (long_edge >= 3000 || short_edge >= 1800) return 2.20f;
    if (long_edge >= 2500 || short_edge >= 1400) return 1.75f;
    if (long_edge >= 1900 || short_edge >= 1050) return 1.25f;
    return 1.00f;
}
}

float recommended_x11_ui_scale(const DisplayScaleInput& input) noexcept {
    float dpi_scale = 1.0f;
    if (std::isfinite(input.dpi) && input.dpi >= 72.0f && input.dpi <= 480.0f) {
        dpi_scale = input.dpi / 96.0f;
    }
    const float scale = std::max(dpi_scale, resolution_floor(input.pixel_width, input.pixel_height));
    return std::clamp(scale, 1.0f, 4.0f);
}


} // namespace such::ui

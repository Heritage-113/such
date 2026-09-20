#include <such/ui/ResponsiveLayout.h>

#include <algorithm>
#include <cmath>

namespace such::ui {

ResponsiveMetrics compute_responsive_metrics(float width_dip, float height_dip) noexcept {
    ResponsiveMetrics m;
    m.width = std::max(width_dip, 360.0f);
    m.height = std::max(height_dip, 300.0f);

    const float scale = std::clamp(m.width / 760.0f, 0.72f, 1.65f);
    const float soft_scale = std::sqrt(scale);

    m.side_padding = std::clamp(m.width * 0.026f, 14.0f, 26.0f);
    m.top_padding = std::clamp(m.height * 0.028f, 12.0f, 22.0f);
    m.bottom_padding = std::clamp(m.height * 0.020f, 10.0f, 18.0f);
    m.search_height = std::clamp(48.0f * soft_scale, 44.0f, 58.0f);
    m.search_to_results_gap = std::clamp(12.0f * soft_scale, 10.0f, 16.0f);

    m.row_gap = std::clamp(7.0f * soft_scale, 5.0f, 9.0f);
    m.row_height = std::clamp(64.0f * soft_scale, 54.0f, 74.0f);

    m.file_icon = std::clamp(34.0f * soft_scale, 30.0f, 42.0f);
    m.icon_gap = std::clamp(12.0f * soft_scale, 10.0f, 14.0f);
    m.extension_badge_height = std::clamp(14.0f * soft_scale, 12.0f, 17.0f);
    m.filename_font = std::clamp(15.0f * soft_scale, 14.0f, 17.0f);
    m.path_font = std::clamp(11.5f * soft_scale, 10.5f, 13.0f);

    m.corner_radius = std::clamp(5.0f * soft_scale, 4.0f, 6.0f);
    m.search_radius = std::clamp(7.0f * soft_scale, 6.0f, 8.0f);
    m.swipe_action_width = std::clamp(72.0f * soft_scale, 66.0f, 82.0f);

    const float results_top = m.top_padding + m.search_height + m.search_to_results_gap;
    const float results_height = std::max(0.0f, m.height - results_top - m.bottom_padding);
    const float stride = m.row_height + m.row_gap;
    m.visible_results = stride > 0.0f
        ? std::max(0, static_cast<int>(std::floor((results_height + m.row_gap) / stride)))
        : 0;
    return m;
}

WindowLayout compute_window_layout(float width_dip, float height_dip) noexcept {
    WindowLayout out;
    out.metrics = compute_responsive_metrics(width_dip, height_dip);
    const auto& m = out.metrics;

    out.search = {m.side_padding, m.top_padding,
                  std::max(1.0f, m.width - m.side_padding * 2.0f), m.search_height};
    const float results_y = out.search.y + out.search.height + m.search_to_results_gap;
    out.results_viewport = {
        m.side_padding,
        results_y,
        std::max(1.0f, m.width - m.side_padding * 2.0f),
        std::max(0.0f, m.height - results_y - m.bottom_padding),
    };
    for (std::size_t i = 0; i < out.rows.size(); ++i) out.rows[i] = result_row_rect(out, i, 0.0f);
    return out;
}

RectF result_row_rect(const WindowLayout& layout, std::size_t index, float scroll_offset_dip) noexcept {
    const auto& m = layout.metrics;
    const float y = layout.results_viewport.y
                  + static_cast<float>(index) * (m.row_height + m.row_gap)
                  - std::max(0.0f, scroll_offset_dip);
    return {layout.results_viewport.x, y, layout.results_viewport.width, m.row_height};
}

float result_content_height(const WindowLayout& layout, std::size_t result_count) noexcept {
    if (result_count == 0) return 0.0f;
    return static_cast<float>(result_count) * layout.metrics.row_height
         + static_cast<float>(result_count - 1) * layout.metrics.row_gap;
}

float max_result_scroll(const WindowLayout& layout, std::size_t result_count) noexcept {
    return std::max(0.0f, result_content_height(layout, result_count) - layout.results_viewport.height);
}

} // namespace such::ui

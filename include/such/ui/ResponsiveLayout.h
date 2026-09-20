#pragma once

#include <array>
#include <cstddef>

namespace such::ui {

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ResponsiveMetrics {
    float width = 0.0f;
    float height = 0.0f;

    float side_padding = 0.0f;
    float top_padding = 0.0f;
    float bottom_padding = 0.0f;
    float search_height = 0.0f;
    float search_to_results_gap = 0.0f;

    float row_height = 0.0f;
    float row_gap = 0.0f;
    float file_icon = 0.0f;
    float icon_gap = 0.0f;
    float extension_badge_height = 0.0f;
    float filename_font = 0.0f;
    float path_font = 0.0f;

    float corner_radius = 0.0f;
    float search_radius = 0.0f;
    float swipe_action_width = 0.0f;

    // This is viewport capacity, not a search-result cap. v1.0 permits an
    // unbounded result list and scrolls/virtualizes rows beyond this count.
    int visible_results = 0;
};

struct WindowLayout {
    ResponsiveMetrics metrics{};
    RectF search{};
    RectF results_viewport{};

    // Compatibility preview for the first five rows. New frontend code should
    // use result_row_rect() so arbitrary result counts remain addressable.
    std::array<RectF, 5> rows{};
};

[[nodiscard]] ResponsiveMetrics compute_responsive_metrics(float width_dip, float height_dip) noexcept;
[[nodiscard]] WindowLayout compute_window_layout(float width_dip, float height_dip) noexcept;
[[nodiscard]] RectF result_row_rect(const WindowLayout& layout, std::size_t index, float scroll_offset_dip = 0.0f) noexcept;
[[nodiscard]] float result_content_height(const WindowLayout& layout, std::size_t result_count) noexcept;
[[nodiscard]] float max_result_scroll(const WindowLayout& layout, std::size_t result_count) noexcept;

} // namespace such::ui

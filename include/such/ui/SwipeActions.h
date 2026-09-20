#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace such::ui {

enum class SwipeSide : std::uint8_t { Inactive, Leading, Trailing };
enum class ResultAction : std::uint8_t {
    OpenLocation,
    ToggleIndex,
    TogglePin,
    OpenProperties,
    EditAppearance,
};

struct ResultSwipeState {
    int row = -1;
    float offset_dip = 0.0f;
    float velocity_dip_s = 0.0f;
    SwipeSide side = SwipeSide::Inactive;
    bool dragging = false;
    bool horizontal_locked = false;
    bool armed_full_action = false;
};

class SwipeController {
public:
    explicit SwipeController(float action_tray_width_dip = 216.0f, bool allow_leading_actions = false) noexcept;

    void set_action_tray_width(float action_tray_width_dip) noexcept;
    void begin(int row, float x_dip, float y_dip) noexcept;
    void update(float x_dip, float y_dip, float dt_seconds) noexcept;
    [[nodiscard]] std::optional<ResultAction> end(float row_width_dip) noexcept;
    void close() noexcept;
    void cancel() noexcept { close(); }

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const ResultSwipeState& state() const noexcept { return state_; }
    [[nodiscard]] bool leading_actions_enabled() const noexcept { return allow_leading_actions_; }

private:
    float tray_width_;
    bool allow_leading_actions_ = false;
    float initial_offset_ = 0.0f;
    float start_x_ = 0.0f;
    float start_y_ = 0.0f;
    float last_x_ = 0.0f;
    bool latched_ = false;
    ResultSwipeState state_{};
};

[[nodiscard]] constexpr std::array<ResultAction, 3> actions_for_side(SwipeSide side) noexcept {
    return side == SwipeSide::Leading
        ? std::array<ResultAction, 3>{ResultAction::TogglePin, ResultAction::OpenProperties, ResultAction::EditAppearance}
        : std::array<ResultAction, 3>{ResultAction::OpenLocation, ResultAction::ToggleIndex, ResultAction::TogglePin};
}

} // namespace such::ui

#include <such/ui/SwipeActions.h>

#include <algorithm>
#include <cmath>

namespace such::ui {

SwipeController::SwipeController(float action_tray_width_dip, bool allow_leading_actions) noexcept
    : tray_width_(std::max(1.0f, action_tray_width_dip)),
      allow_leading_actions_(allow_leading_actions) {}

void SwipeController::set_action_tray_width(float action_tray_width_dip) noexcept {
    tray_width_ = std::max(1.0f, action_tray_width_dip);
    if (is_open() && !state_.dragging) {
        state_.offset_dip = state_.side == SwipeSide::Leading ? tray_width_ : -tray_width_;
    }
}

void SwipeController::begin(int row, float x_dip, float y_dip) noexcept {
    const bool same_open_row = is_open() && state_.row == row;
    initial_offset_ = same_open_row ? state_.offset_dip : 0.0f;

    state_ = {};
    state_.row = row;
    state_.offset_dip = initial_offset_;
    state_.side = initial_offset_ < -0.5f ? SwipeSide::Trailing
                : initial_offset_ > 0.5f ? SwipeSide::Leading
                : SwipeSide::Inactive;
    state_.dragging = true;
    start_x_ = last_x_ = x_dip;
    start_y_ = y_dip;
    latched_ = false;
}

void SwipeController::update(float x_dip, float y_dip, float dt_seconds) noexcept {
    if (!state_.dragging) return;

    const float dx = x_dip - start_x_;
    const float dy = y_dip - start_y_;
    if (!latched_) {
        if (std::abs(dx) < 9.0f && std::abs(dy) < 9.0f) return;
        if (std::abs(dx) <= std::abs(dy) * 1.15f) {
            state_.dragging = false;
            state_.row = -1;
            state_.side = SwipeSide::Inactive;
            return;
        }

        // Canonical Design.md behavior: a closed row only opens on left swipe.
        // A right swipe is meaningful only when closing an already-open trailing row.
        if (!allow_leading_actions_ && initial_offset_ >= -0.5f && dx > 0.0f) {
            state_.dragging = false;
            state_.row = -1;
            state_.side = SwipeSide::Inactive;
            return;
        }

        latched_ = true;
        state_.horizontal_locked = true;
    }

    const float hard = tray_width_ * 1.45f;
    float candidate = initial_offset_ + dx;
    if (allow_leading_actions_) {
        candidate = std::clamp(candidate, -hard, hard);
    } else {
        candidate = std::clamp(candidate, -hard, 0.0f);
    }

    state_.offset_dip = candidate;
    if (candidate < -0.5f) state_.side = SwipeSide::Trailing;
    else if (candidate > 0.5f) state_.side = SwipeSide::Leading;
    else state_.side = SwipeSide::Inactive;

    if (dt_seconds > 0.0001f) {
        state_.velocity_dip_s = (x_dip - last_x_) / dt_seconds;
    }
    last_x_ = x_dip;
}

std::optional<ResultAction> SwipeController::end(float row_width_dip) noexcept {
    if (!state_.dragging) return std::nullopt;
    state_.dragging = false;
    if (!latched_) {
        close();
        return std::nullopt;
    }

    const float distance = std::abs(state_.offset_dip);
    const bool fling = std::abs(state_.velocity_dip_s) >= 900.0f;

    if (allow_leading_actions_ && state_.side == SwipeSide::Leading) {
        // Retained opt-in experiment from the earlier design discussion. It is not
        // part of the authoritative default Design.md contract.
        const float full_threshold = std::max(tray_width_ * 1.10f, row_width_dip * 0.60f);
        state_.armed_full_action = distance >= full_threshold ||
            (fling && distance >= tray_width_ * 0.72f);
        if (state_.armed_full_action) {
            close();
            return ResultAction::TogglePin;
        }
    }

    // If the user dragged an open trailing row back near zero, close it.
    if (state_.side == SwipeSide::Inactive || state_.offset_dip > -tray_width_ * 0.34f) {
        close();
        return std::nullopt;
    }

    const bool should_open = distance >= tray_width_ * 0.34f ||
        (fling && distance >= tray_width_ * 0.22f);
    if (!should_open) {
        close();
        return std::nullopt;
    }

    state_.offset_dip = state_.side == SwipeSide::Leading ? tray_width_ : -tray_width_;
    return std::nullopt;
}

void SwipeController::close() noexcept {
    state_ = {};
    initial_offset_ = 0.0f;
    latched_ = false;
}

bool SwipeController::is_open() const noexcept {
    return state_.row >= 0 && state_.side != SwipeSide::Inactive && std::abs(state_.offset_dip) > 0.5f;
}

} // namespace such::ui

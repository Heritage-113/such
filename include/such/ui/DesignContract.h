#pragma once

#include <cstdint>

namespace such::ui::design {

// v1.1: five is the compact-window design target, not a search-result cap.
inline constexpr int kCompactViewportTarget = 5;
inline constexpr float kSearchRadiusMin = 6.0f;
inline constexpr float kSearchRadiusMax = 8.0f;
inline constexpr float kRowRadiusMin = 4.0f;
inline constexpr float kRowRadiusMax = 6.0f;
inline constexpr float kBadgeRadius = 3.0f;

inline constexpr std::uint32_t kPaperRgb = 0xF3EFE5u;
inline constexpr std::uint32_t kBrightSurfaceRgb = 0xFBF8EFu;
inline constexpr std::uint32_t kDarkGreenRgb = 0x173A2Bu;
inline constexpr const char* kSearchPlaceholder = "2026 Heritage Inc.";

// Canonical gesture remains left swipe => Location / Index / Pin. Right swipe
// closes. Additional leading-side actions stay opt-in.
inline constexpr bool kCanonicalLeadingManagementSwipe = false;

} // namespace such::ui::design

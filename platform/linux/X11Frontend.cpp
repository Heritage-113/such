#include <such/Version.h>
#include <such/cli/CliApp.h>
#include <such/runtime/RuntimeClient.h>
#include <such/security/SecureSearchGate.h>
#include <such/ui/AgentLauncher.h>
#include <such/ui/DesignContract.h>
#include <such/ui/DisplayScalePolicy.h>
#include <such/ui/IndexCommands.h>
#include <such/ui/FrontendLease.h>
#include <such/ui/FontSettings.h>
#include <such/ui/ResponsiveLayout.h>
#include <such/ui/ResultView.h>
#include <such/ui/SearchDialect.h>
#include <such/ui/SwipeActions.h>

#include "LinuxNativeIcons.h"
#include "LinuxIndexRoots.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <clocale>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <poll.h>
#include <pwd.h>
#include <unistd.h>

namespace {
#if defined(SUCH_EXPERIMENTAL_LEADING_SWIPE)
constexpr bool kAllowLeadingSwipe = true;
#else
constexpr bool kAllowLeadingSwipe = false;
#endif
constexpr unsigned long kPaper = 0xF3EFE5;
constexpr unsigned long kBright = 0xFBF8EF;
constexpr unsigned long kDarkGreen = 0x173A2B;
constexpr unsigned long kPathGreen = 0x54675B;
constexpr unsigned long kPlaceholderGray = 0xB8B5AD;
constexpr unsigned long kHairline = 0xD3CDBF;
constexpr unsigned long kHighlight = 0xFFFDF7;
constexpr unsigned long kAction2 = 0x224C3A;
constexpr unsigned long kAction3 = 0x2F5B46;

XFontSet gFontSet = nullptr;
XIM gInputMethod = nullptr;
XIC gInputContext = nullptr;

struct UiState {
    std::string query;
    std::vector<such::ui::ResultItem> results;
    std::vector<such::ui::Suggestion> suggestions;
    std::string search_error;
    such::ui::SwipeController swipe{216.0f, kAllowLeadingSwipe};
    int selected = -1;
    bool demo = false;
    bool smoke = false;
    int width = 760;
    int height = 600;
    std::chrono::steady_clock::time_point last_motion = std::chrono::steady_clock::now();
    such::platform::linuxui::NativeIconCache icon_cache;
    std::string font_family;
    such::runtime::RuntimeClient runtime;
    std::uint64_t runtime_generation = 0;
    std::size_t runtime_indexed_files = 0;
    bool runtime_indexing = false;
    such::security::SecureSearchGate security_gate;
    bool security_notice_open = false;
    float scroll_dip = 0.0f;
    float ui_scale = 1.0f;
    such::ui::IndexCommandKind root_picker_kind = such::ui::IndexCommandKind::NoCommand;
};

int dip_px(const UiState& state, float dip) {
    return static_cast<int>(std::lround(dip * state.ui_scale));
}

such::ui::RectF scale_rect(such::ui::RectF r, float scale) {
    r.x *= scale; r.y *= scale; r.width *= scale; r.height *= scale;
    return r;
}

such::ui::WindowLayout x11_layout(const UiState& state) {
    const float scale = std::max(0.5f, state.ui_scale);
    auto out = such::ui::compute_window_layout(
        static_cast<float>(state.width) / scale, static_cast<float>(state.height) / scale);
    auto& m = out.metrics;
    auto mul = [scale](float& v) { v *= scale; };
    mul(m.width); mul(m.height); mul(m.side_padding); mul(m.top_padding); mul(m.bottom_padding);
    mul(m.search_height); mul(m.search_to_results_gap); mul(m.security_height); mul(m.security_to_results_gap); mul(m.row_height); mul(m.row_gap);
    mul(m.file_icon); mul(m.icon_gap); mul(m.extension_badge_height); mul(m.filename_font);
    mul(m.path_font); mul(m.corner_radius); mul(m.search_radius); mul(m.swipe_action_width);
    out.search = scale_rect(out.search, scale);
    out.security = scale_rect(out.security, scale);
    out.results_viewport = scale_rect(out.results_viewport, scale);
    for (auto& row : out.rows) row = scale_rect(row, scale);

    const auto detail = such::ui::parse_detail_search(state.query);
    if (detail.active) {
        const float branch_h = 32.0f * scale;
        const float branch_gap = 6.0f * scale;
        const float total = static_cast<float>(detail.refinements.size()) * (branch_h + branch_gap);
        out.results_viewport.y += total;
        out.results_viewport.height = std::max(0.0f, out.results_viewport.height - total);
        const float stride = m.row_height + m.row_gap;
        m.visible_results = stride > 0.0f ? std::max(0, static_cast<int>(std::floor((out.results_viewport.height + m.row_gap) / stride))) : 0;
    }
    return out;
}

float detect_ui_scale(Display* d, int screen) {
    if (const char* explicit_scale = std::getenv("SUCH_UI_SCALE"); explicit_scale && *explicit_scale) {
        char* end = nullptr;
        const float v = std::strtof(explicit_scale, &end);
        if (end != explicit_scale && std::isfinite(v)) return std::clamp(v, 0.75f, 4.0f);
    }

    such::ui::DisplayScaleInput input;
    input.pixel_width = DisplayWidth(d, screen);
    input.pixel_height = DisplayHeight(d, screen);
    const int mm = DisplayWidthMM(d, screen);
    if (mm > 0 && input.pixel_width > 0) {
        input.dpi = static_cast<float>(input.pixel_width) * 25.4f / static_cast<float>(mm);
    }
    return such::ui::recommended_x11_ui_scale(input);
}

void set_color(Display* d, GC gc, unsigned long color) {
    (void)d;
    XSetForeground(d, gc, color);
}

void draw_utf8(Display* d, Window w, GC gc, int x, int y, const std::string& text) {
    if (text.empty()) return;
    if (gFontSet) {
        Xutf8DrawString(d, w, gFontSet, gc, x, y, text.c_str(), static_cast<int>(text.size()));
    } else {
        XDrawString(d, w, gc, x, y, text.c_str(), static_cast<int>(text.size()));
    }
}

int utf8_text_width(const std::string& text) {
    if (text.empty()) return 0;
    if (gFontSet) {
        XRectangle ink{};
        XRectangle logical{};
        Xutf8TextExtents(gFontSet, text.c_str(), static_cast<int>(text.size()), &ink, &logical);
        return logical.width;
    }
    return static_cast<int>(text.size()) * 7;
}

void pop_utf8_codepoint(std::string& text) {
    if (text.empty()) return;
    text.pop_back();
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0u) == 0x80u) text.pop_back();
}

std::string ellipsize_utf8(std::string text, int max_width) {
    if (max_width <= 0) return {};
    if (utf8_text_width(text) <= max_width) return text;
    constexpr const char* suffix = "...";
    const int suffix_width = utf8_text_width(suffix);
    while (!text.empty() && utf8_text_width(text) + suffix_width > max_width) pop_utf8_codepoint(text);
    return text + suffix;
}

void fill_round_rect(Display* d, Window w, GC gc, int x, int y, int width, int height, int radius, unsigned long color) {
    if (width <= 0 || height <= 0) return;
    radius = std::clamp(radius, 0, std::min(width, height) / 2);
    set_color(d, gc, color);
    if (radius == 0) {
        XFillRectangle(d, w, gc, x, y, static_cast<unsigned>(width), static_cast<unsigned>(height));
        return;
    }
    XFillRectangle(d, w, gc, x + radius, y, static_cast<unsigned>(std::max(1, width - radius * 2)), static_cast<unsigned>(height));
    XFillRectangle(d, w, gc, x, y + radius, static_cast<unsigned>(width), static_cast<unsigned>(std::max(1, height - radius * 2)));
    const int dia = radius * 2;
    XFillArc(d, w, gc, x, y, static_cast<unsigned>(dia), static_cast<unsigned>(dia), 90 * 64, 90 * 64);
    XFillArc(d, w, gc, x + width - dia, y, static_cast<unsigned>(dia), static_cast<unsigned>(dia), 0, 90 * 64);
    XFillArc(d, w, gc, x, y + height - dia, static_cast<unsigned>(dia), static_cast<unsigned>(dia), 180 * 64, 90 * 64);
    XFillArc(d, w, gc, x + width - dia, y + height - dia, static_cast<unsigned>(dia), static_cast<unsigned>(dia), 270 * 64, 90 * 64);
}

void stroke_round_rect(Display* d, Window w, GC gc, int x, int y, int width, int height, int radius, unsigned long color, int line_width = 1) {
    set_color(d, gc, color);
    XSetLineAttributes(d, gc, std::max(1, line_width), LineSolid, CapRound, JoinRound);
    XDrawRectangle(d, w, gc, x, y, static_cast<unsigned>(std::max(1, width - 1)), static_cast<unsigned>(std::max(1, height - 1)));
    XSetLineAttributes(d, gc, 1, LineSolid, CapButt, JoinMiter);
    (void)radius; // Core X11 has no antialiased rounded stroke; fill shape carries the small radius.
}

void draw_search_glyph(Display* d, Window w, GC gc, int cx, int cy, float scale, unsigned long color = kDarkGreen) {
    set_color(d, gc, color);
    XSetLineAttributes(d, gc, std::max(1, static_cast<int>(std::lround(scale))), LineSolid, CapRound, JoinRound);
    const int r = std::max(4, static_cast<int>(std::lround(6.0f * scale)));
    const int tail0 = std::max(3, static_cast<int>(std::lround(4.0f * scale)));
    const int tail1 = std::max(tail0 + 3, static_cast<int>(std::lround(9.0f * scale)));
    XDrawArc(d, w, gc, cx - r, cy - r, static_cast<unsigned>(r * 2), static_cast<unsigned>(r * 2), 0, 360 * 64);
    XDrawLine(d, w, gc, cx + tail0, cy + tail0, cx + tail1, cy + tail1);
    XSetLineAttributes(d, gc, 1, LineSolid, CapButt, JoinMiter);
}

void draw_pin(Display* d, Window w, GC gc, int x, int y, float scale) {
    set_color(d, gc, kDarkGreen);
    const int head_w = std::max(6, static_cast<int>(std::lround(8.0f * scale)));
    const int head_h = std::max(4, static_cast<int>(std::lround(5.0f * scale)));
    const int cx = x + head_w / 2;
    const int stem_end = y + std::max(head_h + 5, static_cast<int>(std::lround(11.0f * scale)));
    XFillArc(d, w, gc, x, y, static_cast<unsigned>(head_w), static_cast<unsigned>(head_h), 0, 360 * 64);
    XDrawLine(d, w, gc, cx, y + head_h / 2, cx, stem_end);
    XDrawLine(d, w, gc, cx, stem_end, cx - std::max(2, static_cast<int>(std::lround(2.0f * scale))),
              stem_end + std::max(2, static_cast<int>(std::lround(3.0f * scale))));
}

void draw_badge(Display* d, Window w, GC gc, const std::string& ext, int xRight, int yBottom, float scale) {
    if (ext.empty()) return;
    std::string up = ext;
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (up.size() > 5) up.resize(5);
    const int bw = static_cast<int>(std::lround(static_cast<float>(up.size()) * 7.0f * scale + 10.0f * scale));
    const int bh = static_cast<int>(std::lround(16.0f * scale));
    fill_round_rect(d, w, gc, xRight - bw, yBottom - bh, bw, bh, std::max(2, static_cast<int>(std::lround(3.0f * scale))), kDarkGreen);
    set_color(d, gc, 0xFFFFFF);
    draw_utf8(d, w, gc, xRight - bw + static_cast<int>(std::lround(5.0f * scale)), yBottom - static_cast<int>(std::lround(4.0f * scale)), up);
}

void draw_action_tray(Display* d, Window w, GC gc, const such::ui::RectF& row, such::ui::SwipeSide side,
                      const such::ui::ResponsiveMetrics& m, const such::ui::ResultItem& item) {
    if (side == such::ui::SwipeSide::Inactive) return;
    const int aw = static_cast<int>(std::lround(m.swipe_action_width));
    const int total = aw * 3;
    const int rowX = static_cast<int>(std::lround(row.x));
    const int rowY = static_cast<int>(std::lround(row.y));
    const int rowW = static_cast<int>(std::lround(row.width));
    const int rowH = static_cast<int>(std::lround(row.height));
    const int x0 = side == such::ui::SwipeSide::Trailing ? rowX + rowW - total : rowX;
    const auto actions = such::ui::actions_for_side(side);
    const std::array<unsigned long, 3> colors{kDarkGreen, kAction2, kAction3};
    for (int i = 0; i < 3; ++i) {
        set_color(d, gc, colors[static_cast<std::size_t>(i)]);
        XFillRectangle(d, w, gc, x0 + i * aw, rowY, static_cast<unsigned>(aw), static_cast<unsigned>(rowH));
        set_color(d, gc, 0xFFFFFF);
        const auto action = actions[static_cast<std::size_t>(i)];
        const char* label = "";
        switch (action) {
            case such::ui::ResultAction::OpenLocation: label = "Location"; break;
            case such::ui::ResultAction::ToggleIndex: label = item.indexed ? "Unindex" : "Index"; break;
            case such::ui::ResultAction::TogglePin: label = item.pinned ? "Unpin" : "Pin"; break;
            case such::ui::ResultAction::OpenProperties: label = "Properties"; break;
            case such::ui::ResultAction::EditAppearance: label = "Appearance"; break;
        }
        draw_utf8(d, w, gc, x0 + i * aw + 7, rowY + rowH / 2 + 4, label);
    }
}


void draw_result(Display* d, Window w, GC gc, const such::ui::ResultItem& item, const such::ui::RectF& rf,
                 int index, UiState& state) {
    const auto& m = x11_layout(state).metrics;
    if (state.swipe.state().row == index && state.swipe.state().side != such::ui::SwipeSide::Inactive) {
        draw_action_tray(d, w, gc, rf, state.swipe.state().side, m, item);
    }
    const int offset = state.swipe.state().row == index ? dip_px(state, state.swipe.state().offset_dip) : 0;
    const int x = static_cast<int>(std::lround(rf.x)) + offset;
    const int y = static_cast<int>(std::lround(rf.y));
    const int rw = static_cast<int>(std::lround(rf.width));
    const int rh = static_cast<int>(std::lround(rf.height));
    fill_round_rect(d, w, gc, x, y, rw, rh, static_cast<int>(std::lround(m.corner_radius)), kBright);
    stroke_round_rect(d, w, gc, x, y, rw, rh, static_cast<int>(std::lround(m.corner_radius)),
                      state.selected == index ? kDarkGreen : kHairline);
    set_color(d, gc, kHighlight);
    XDrawLine(d, w, gc, x + 8, y + 1, x + rw - 8, y + 1);

    const int icon = static_cast<int>(std::lround(m.file_icon));
    const int ix = x + dip_px(state, 12.0f);
    const int iy = y + (rh - icon) / 2;
    const auto& nativeIcon = state.icon_cache.icon_for_extension(item.extension);
    state.icon_cache.draw(d, w, gc, nativeIcon, ix, iy, icon, kBright);
    draw_badge(d, w, gc, item.extension, ix + icon + dip_px(state, 5.0f), iy + icon + dip_px(state, 3.0f), state.ui_scale);

    const int tx = ix + icon + static_cast<int>(std::lround(m.icon_gap));
    const int text_width = std::max(8, rw - (tx - x) - dip_px(state, item.pinned ? 34.0f : 18.0f));
    set_color(d, gc, kDarkGreen);
    draw_utf8(d, w, gc, tx, y + dip_px(state, 25.0f), ellipsize_utf8(item.filename, text_width));
    set_color(d, gc, kPathGreen);
    draw_utf8(d, w, gc, tx, y + dip_px(state, 43.0f), ellipsize_utf8(item.path, std::max(8, rw - (tx - x) - dip_px(state, 18.0f))));
    if (item.pinned) draw_pin(d, w, gc, x + rw - dip_px(state, 24.0f), y + dip_px(state, 12.0f), state.ui_scale);
}

std::vector<such::ui::ResultItem> root_items(UiState& state) {
    std::vector<such::ui::ResultItem> out;
    std::string error;
    const auto roots = state.runtime.roots(&error);
    if (!error.empty()) {
        state.search_error = "Could not list search roots: " + error;
        return out;
    }
    for (const auto& root : roots) {
        such::ui::ResultItem item;
        item.path = root;
        item.filename = std::filesystem::path(root).filename().string();
        if (item.filename.empty()) item.filename = root;
        item.indexed = true;
        out.push_back(std::move(item));
    }
    return out;
}

std::vector<such::ui::ResultItem> index_root_candidate_items() {
    std::vector<such::ui::ResultItem> out;
    for (const auto& candidate : such::platform::linuxui::discover_index_root_candidates()) {
        such::ui::ResultItem item;
        item.path = candidate.path.string();
        item.filename = candidate.label;
        item.indexed = false;
        item.icon_override = such::ui::IconOverride::Folder;
        out.push_back(std::move(item));
    }
    return out;
}

void refresh_query(UiState& state, bool reset_scroll = true) {
    state.search_error.clear();
    const auto fontCommand = such::ui::parse_font_command(state.query, such::ui::PlatformDialect::UnixLike);
    const auto agentCommand = such::ui::parse_agent_command(state.query, such::ui::PlatformDialect::UnixLike);
    const auto indexCommand = such::ui::parse_index_command(state.query, such::ui::PlatformDialect::UnixLike);
    const auto observed = state.runtime.observed_extensions();
    const auto previous_root_picker_kind = state.root_picker_kind;
    state.root_picker_kind = such::ui::IndexCommandKind::NoCommand;
    if (agentCommand.matched) {
        state.results.clear();
        state.suggestions.clear();
    } else if (fontCommand.matched) {
        state.results.clear();
        state.suggestions.clear();
        if (fontCommand.show_picker) {
            for (const auto& option : such::ui::builtin_font_options()) {
                state.suggestions.push_back({"//font " + option.alias, option.label});
            }
            state.suggestions.push_back({"//font system", "System default"});
        }
    } else if (indexCommand.matched) {
        if ((indexCommand.kind == such::ui::IndexCommandKind::AddRoot ||
             indexCommand.kind == such::ui::IndexCommandKind::ReplaceRoot) &&
            !indexCommand.argument.has_value()) {
            // Linux has no drive letters. Treat mounted filesystems as drives and
            // present them directly in Such rather than depending on a desktop-
            // specific external folder-picker package.
            state.root_picker_kind = indexCommand.kind;
            if (previous_root_picker_kind != indexCommand.kind) state.selected = -1;
            state.suggestions.clear();
            state.results = index_root_candidate_items();
            if (state.results.empty()) {
                state.suggestions.push_back({
                    indexCommand.kind == such::ui::IndexCommandKind::ReplaceRoot ? "//drive /path/to/folder" : "//index /path/to/folder",
                    "No mounted folders detected; type a path explicitly"});
            }
        } else {
            state.suggestions = such::ui::autocomplete(state.query, such::ui::PlatformDialect::UnixLike, observed);
            if (indexCommand.kind == such::ui::IndexCommandKind::ShowRoots) state.results = root_items(state);
            else state.results.clear();
        }
    } else {
        state.suggestions = such::ui::autocomplete(state.query, such::ui::PlatformDialect::UnixLike, observed);
        if (state.demo) {
            state.results = such::ui::make_demo_results(state.query, such::ui::PlatformDialect::UnixLike,
                                                       static_cast<std::int64_t>(std::time(nullptr)));
        } else {
            state.results = state.runtime.search(state.query, such::ui::PlatformDialect::UnixLike, 0, &state.search_error);
        }
    }
    if (state.results.empty()) {
        state.selected = -1;
    } else if (state.root_picker_kind != such::ui::IndexCommandKind::NoCommand && state.selected < 0) {
        // Do not silently pick HOME merely because the user pressed Enter after
        // typing //drive. Arrow/click selection must be explicit.
        state.selected = -1;
    } else {
        state.selected = std::clamp(state.selected, 0, static_cast<int>(state.results.size()) - 1);
    }
    if (reset_scroll) state.scroll_dip = 0.0f;
    const auto layout = x11_layout(state);
    state.scroll_dip = std::clamp(state.scroll_dip, 0.0f, such::ui::max_result_scroll(layout, state.results.size()));
    state.swipe.close();
}

int hit_row(const UiState& state, int x, int y) {
    const auto layout = x11_layout(state);
    const float xf = static_cast<float>(x);
    const float yf = static_cast<float>(y);
    const auto& viewport = layout.results_viewport;
    if (xf < viewport.x || xf >= viewport.x + viewport.width || yf < viewport.y || yf >= viewport.y + viewport.height) return -1;
    const float stride = layout.metrics.row_height + layout.metrics.row_gap;
    if (stride <= 0.0f) return -1;
    const float content_y = yf - viewport.y + state.scroll_dip;
    const int index = static_cast<int>(std::floor(content_y / stride));
    if (index < 0 || index >= static_cast<int>(state.results.size())) return -1;
    const auto row = such::ui::result_row_rect(layout, static_cast<std::size_t>(index), state.scroll_dip);
    return (yf >= row.y && yf < row.y + row.height) ? index : -1;
}

void ensure_selected_visible(UiState& state) {
    if (state.selected < 0) return;
    const auto layout = x11_layout(state);
    const auto row = such::ui::result_row_rect(layout, static_cast<std::size_t>(state.selected), state.scroll_dip);
    if (row.y < layout.results_viewport.y) {
        state.scroll_dip = static_cast<float>(state.selected) * (layout.metrics.row_height + layout.metrics.row_gap);
    } else if (row.y + row.height > layout.results_viewport.y + layout.results_viewport.height) {
        state.scroll_dip += row.y + row.height - (layout.results_viewport.y + layout.results_viewport.height);
    }
    state.scroll_dip = std::clamp(state.scroll_dip, 0.0f, such::ui::max_result_scroll(layout, state.results.size()));
}

bool hit_swipe_action(const UiState& state, int x, int y, int& row_index, such::ui::ResultAction& action) {
    if (!state.swipe.is_open()) return false;
    row_index = state.swipe.state().row;
    if (row_index < 0 || row_index >= static_cast<int>(state.results.size())) return false;
    const auto layout = x11_layout(state);
    const auto row = such::ui::result_row_rect(layout, static_cast<std::size_t>(row_index), state.scroll_dip);
    const float xf = static_cast<float>(x);
    const float yf = static_cast<float>(y);
    if (yf < row.y || yf >= row.y + row.height) return false;
    const float aw = layout.metrics.swipe_action_width;
    const float total = aw * 3.0f;
    const float offset = state.swipe.state().offset_dip;
    int index = -1;
    if (state.swipe.state().side == such::ui::SwipeSide::Trailing) {
        const float start = row.x + row.width - total;
        const float revealed_start = std::max(start, row.x + row.width + offset);
        if (xf >= revealed_start && xf < row.x + row.width) index = static_cast<int>((xf - start) / aw);
    } else {
        const float end = row.x + total;
        const float revealed_end = std::min(end, row.x + offset);
        if (xf >= row.x && xf < revealed_end) index = static_cast<int>((xf - row.x) / aw);
    }
    if (index < 0 || index > 2) return false;
    action = such::ui::actions_for_side(state.swipe.state().side)[static_cast<std::size_t>(index)];
    return true;
}

void open_item_linux(const such::ui::ResultItem& item) {
    if (item.path.empty()) return;
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::execlp("xdg-open", "xdg-open", item.path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
}

void open_location_linux(const such::ui::ResultItem& item) {
    std::filesystem::path path(item.path);
    std::filesystem::path directory = path.has_parent_path() ? path.parent_path() : path;
    if (directory.empty()) return;
    const std::string dir = directory.string();
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::execlp("xdg-open", "xdg-open", dir.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
}

void perform_action(UiState& state, int row_index, such::ui::ResultAction action) {
    if (row_index < 0 || row_index >= static_cast<int>(state.results.size())) return;
    auto& item = state.results[static_cast<std::size_t>(row_index)];
    switch (action) {
        case such::ui::ResultAction::OpenLocation:
            open_location_linux(item);
            break;
        case such::ui::ResultAction::ToggleIndex:
            if (state.demo) item.indexed = !item.indexed;
            else {
                std::string error;
                if (!state.runtime.set_indexed(item.path, !item.indexed, &error)) {
                    std::fprintf(stderr, "Such: could not change index state: %s\n", error.c_str());
                }
                refresh_query(state, false);
            }
            break;
        case such::ui::ResultAction::TogglePin:
            if (state.demo) item.pinned = !item.pinned;
            else {
                std::string error;
                if (!state.runtime.set_pinned(item.path, !item.pinned, &error)) {
                    std::fprintf(stderr, "Such: could not change pin state: %s\n", error.c_str());
                }
                refresh_query(state, false);
            }
            break;
        case such::ui::ResultAction::OpenProperties:
        case such::ui::ResultAction::EditAppearance:
            // No universal Linux native properties/appearance API. These are only
            // reachable when the non-canonical leading experiment is enabled.
            break;
    }
    state.swipe.close();
}

void complete_first(UiState& state) {
    if (state.suggestions.empty()) return;
    const auto pos = state.query.find_last_of(" \t\n");
    state.query = (pos == std::string::npos ? std::string{} : state.query.substr(0, pos + 1)) + state.suggestions.front().token + " ";
    refresh_query(state);
}

void draw_detail_tree(Display* d, Window w, GC gc, const UiState& state, const such::ui::WindowLayout& layout) {
    const auto detail = such::ui::parse_detail_search(state.query);
    if (!detail.active) return;
    const int branch_h = dip_px(state, 32.0f);
    const int gap = dip_px(state, 6.0f);
    const int indent_step = dip_px(state, 18.0f);
    int y = static_cast<int>(layout.security.y + layout.security.height + layout.metrics.security_to_results_gap);
    for (std::size_t i = 0; i < detail.refinements.size(); ++i) {
        const int indent = static_cast<int>(i + 1u) * indent_step;
        const int x = static_cast<int>(layout.search.x) + indent;
        const int max_w = std::max(dip_px(state, 150.0f), static_cast<int>(layout.search.width) - indent - dip_px(state, 48.0f));
        const int wbox = std::max(dip_px(state, 170.0f), std::min(max_w, static_cast<int>(layout.search.width * 0.72f)));
        set_color(d, gc, kHairline);
        XDrawLine(d, w, gc, x - dip_px(state, 10.0f), y - gap, x - dip_px(state, 10.0f), y + branch_h / 2);
        XDrawLine(d, w, gc, x - dip_px(state, 10.0f), y + branch_h / 2, x, y + branch_h / 2);
        fill_round_rect(d, w, gc, x, y, wbox, branch_h, dip_px(state, 6.0f), kBright);
        stroke_round_rect(d, w, gc, x, y, wbox, branch_h, dip_px(state, 6.0f), kHairline);
        set_color(d, gc, detail.refinements[i].empty() ? kPlaceholderGray : kDarkGreen);
        const std::string text = detail.refinements[i].empty() ? "Detail search..." : detail.refinements[i];
        draw_utf8(d, w, gc, x + dip_px(state, 12.0f), y + dip_px(state, 21.0f),
                  ellipsize_utf8(text, wbox - dip_px(state, 24.0f)));
        y += branch_h + gap;
    }
}


bool point_in_rect(const such::ui::RectF& r, int x, int y) {
    return static_cast<float>(x) >= r.x && static_cast<float>(x) < r.x + r.width &&
           static_cast<float>(y) >= r.y && static_cast<float>(y) < r.y + r.height;
}

void draw_security_toggle(Display* d, Window w, GC gc, const UiState& state,
                          const such::ui::WindowLayout& layout) {
    const auto& r = layout.security;
    const int y_mid = static_cast<int>(r.y + r.height * 0.5f);
    set_color(d, gc, kDarkGreen);
    draw_utf8(d, w, gc,
              static_cast<int>(r.x + dip_px(state, 2.0f)),
              y_mid + dip_px(state, 5.0f),
              "Security");

    const int switch_w = dip_px(state, 34.0f);
    const int switch_h = dip_px(state, 18.0f);
    const int switch_x = static_cast<int>(r.x) + dip_px(state, 72.0f);
    const int switch_y = y_mid - switch_h / 2;
    const bool active = state.security_gate.active();
    fill_round_rect(d, w, gc, switch_x, switch_y, switch_w, switch_h, switch_h / 2,
                    active ? kDarkGreen : kHairline);
    const int knob = std::max(10, switch_h - dip_px(state, 4.0f));
    const int knob_x = active
        ? switch_x + switch_w - knob - dip_px(state, 2.0f)
        : switch_x + dip_px(state, 2.0f);
    const int knob_y = switch_y + (switch_h - knob) / 2;
    fill_round_rect(d, w, gc, knob_x, knob_y, knob, knob, knob / 2, kBright);
}

such::ui::RectF security_notice_panel(const UiState& state) {
    const float width = std::min(static_cast<float>(state.width) - dip_px(state, 32.0f),
                                 static_cast<float>(dip_px(state, 520.0f)));
    const float height = static_cast<float>(dip_px(state, 190.0f));
    return {(static_cast<float>(state.width) - width) * 0.5f,
            (static_cast<float>(state.height) - height) * 0.5f,
            width, height};
}

such::ui::RectF security_notice_contact_button(const UiState& state) {
    const auto panel = security_notice_panel(state);
    return {panel.x + dip_px(state, 24.0f),
            panel.y + panel.height - dip_px(state, 48.0f),
            static_cast<float>(dip_px(state, 145.0f)),
            static_cast<float>(dip_px(state, 30.0f))};
}

such::ui::RectF security_notice_cancel_button(const UiState& state) {
    const auto panel = security_notice_panel(state);
    return {panel.x + panel.width - dip_px(state, 104.0f),
            panel.y + panel.height - dip_px(state, 48.0f),
            static_cast<float>(dip_px(state, 80.0f)),
            static_cast<float>(dip_px(state, 30.0f))};
}

void draw_security_notice(Display* d, Window w, GC gc, const UiState& state) {
    if (!state.security_notice_open) return;
    const auto panel = security_notice_panel(state);
    fill_round_rect(d, w, gc, static_cast<int>(panel.x), static_cast<int>(panel.y),
                    static_cast<int>(panel.width), static_cast<int>(panel.height),
                    dip_px(state, 8.0f), kBright);
    stroke_round_rect(d, w, gc, static_cast<int>(panel.x), static_cast<int>(panel.y),
                      static_cast<int>(panel.width), static_cast<int>(panel.height),
                      dip_px(state, 8.0f), kDarkGreen, dip_px(state, 1.0f));

    set_color(d, gc, kDarkGreen);
    draw_utf8(d, w, gc, static_cast<int>(panel.x) + dip_px(state, 24.0f),
              static_cast<int>(panel.y) + dip_px(state, 38.0f),
              "보안검색은 본사와 연락이 필요합니다.");
    set_color(d, gc, kPathGreen);
    draw_utf8(d, w, gc, static_cast<int>(panel.x) + dip_px(state, 24.0f),
              static_cast<int>(panel.y) + dip_px(state, 70.0f),
              "Enterprise Such needs connection to corporation");
    draw_utf8(d, w, gc, static_cast<int>(panel.x) + dip_px(state, 24.0f),
              static_cast<int>(panel.y) + dip_px(state, 98.0f),
              such::security::kEnterpriseSecurityUrl);

    const auto contact = security_notice_contact_button(state);
    fill_round_rect(d, w, gc, static_cast<int>(contact.x), static_cast<int>(contact.y),
                    static_cast<int>(contact.width), static_cast<int>(contact.height),
                    dip_px(state, 5.0f), kDarkGreen);
    set_color(d, gc, kBright);
    draw_utf8(d, w, gc, static_cast<int>(contact.x) + dip_px(state, 12.0f),
              static_cast<int>(contact.y) + dip_px(state, 21.0f), "Contact Heritage");

    const auto cancel = security_notice_cancel_button(state);
    fill_round_rect(d, w, gc, static_cast<int>(cancel.x), static_cast<int>(cancel.y),
                    static_cast<int>(cancel.width), static_cast<int>(cancel.height),
                    dip_px(state, 5.0f), kPaper);
    stroke_round_rect(d, w, gc, static_cast<int>(cancel.x), static_cast<int>(cancel.y),
                      static_cast<int>(cancel.width), static_cast<int>(cancel.height),
                      dip_px(state, 5.0f), kHairline);
    set_color(d, gc, kDarkGreen);
    draw_utf8(d, w, gc, static_cast<int>(cancel.x) + dip_px(state, 15.0f),
              static_cast<int>(cancel.y) + dip_px(state, 21.0f), "Cancel");
}


void request_security_toggle(UiState& state) {
    if (state.security_gate.active()) {
        (void)state.security_gate.request_disable();
        state.security_notice_open = false;
        return;
    }

    std::string error;
    const auto decision = state.security_gate.request_enable(error);
    if (decision == such::security::SecureSearchDecision::RequiresEnterpriseActivation) {
        state.security_notice_open = true;
        return;
    }
    if (decision == such::security::SecureSearchDecision::Denied) {
        state.search_error = error.empty() ? "Secure Search activation was denied" : std::move(error);
    }
}

void open_enterprise_url_linux() {
    const pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", such::security::kEnterpriseSecurityUrl, static_cast<char*>(nullptr));
        _exit(127);
    }
}

void draw(Display* d, Window w, GC gc, UiState& state) {
    const auto layout = x11_layout(state);
    const auto& m = layout.metrics;
    const auto draw_status = state.demo ? such::runtime::RuntimeStatus{} : state.runtime.status();
    set_color(d, gc, kPaper);
    XFillRectangle(d, w, gc, 0, 0, static_cast<unsigned>(state.width), static_cast<unsigned>(state.height));

    const auto& sr = layout.search;
    const unsigned long command_fill = kBright;
    const unsigned long command_text = state.query.empty() ? kPlaceholderGray : kDarkGreen;
    const unsigned long command_glyph = kDarkGreen;
    fill_round_rect(d, w, gc, static_cast<int>(sr.x), static_cast<int>(sr.y), static_cast<int>(sr.width), static_cast<int>(sr.height),
                    static_cast<int>(m.search_radius), command_fill);
    stroke_round_rect(d, w, gc, static_cast<int>(sr.x), static_cast<int>(sr.y), static_cast<int>(sr.width), static_cast<int>(sr.height),
                      static_cast<int>(m.search_radius), kDarkGreen, dip_px(state, 1.25f));
    set_color(d, gc, kHighlight);
    XDrawLine(d, w, gc, static_cast<int>(sr.x + 8), static_cast<int>(sr.y + 1), static_cast<int>(sr.x + sr.width - 8), static_cast<int>(sr.y + 1));
    draw_search_glyph(d, w, gc, static_cast<int>(sr.x + dip_px(state, 20.0f)), static_cast<int>(sr.y + sr.height / 2), state.ui_scale, command_glyph);
    set_color(d, gc, command_text);
    const std::string label = state.query.empty() ? "2026 Heritage Inc." : state.query;
    draw_utf8(d, w, gc, static_cast<int>(sr.x + dip_px(state, 39.0f)), static_cast<int>(sr.y + sr.height / 2 + dip_px(state, 5.0f)),
              ellipsize_utf8(label, std::max(8, static_cast<int>(sr.width) - dip_px(state, 58.0f))));
    draw_security_toggle(d, w, gc, state, layout);
    draw_detail_tree(d, w, gc, state, layout);

    const float stride = m.row_height + m.row_gap;
    const int first_row = stride > 0.0f ? std::max(0, static_cast<int>(std::floor(state.scroll_dip / stride))) : 0;
    const int last_row = std::min(static_cast<int>(state.results.size()), first_row + std::max(2, m.visible_results + 2));
    for (int i = first_row; i < last_row; ++i) {
        const auto row = such::ui::result_row_rect(layout, static_cast<std::size_t>(i), state.scroll_dip);
        if (row.y + row.height <= layout.results_viewport.y || row.y >= layout.results_viewport.y + layout.results_viewport.height) continue;
        draw_result(d, w, gc, state.results[static_cast<std::size_t>(i)], row, i, state);
    }

    if (!state.demo && state.query.empty() && draw_status.indexing) {
        set_color(d, gc, kPathGreen);
        const std::string message = "Indexing files... " + std::to_string(draw_status.indexed_files);
        draw_utf8(d, w, gc, static_cast<int>(layout.results_viewport.x), static_cast<int>(layout.results_viewport.y + 28), message);
    } else if (!state.query.empty() && state.results.empty() && state.suggestions.empty()) {
        set_color(d, gc, kPathGreen);
        const std::string message = !state.search_error.empty()
            ? "Search error: " + state.search_error
            : ((!state.demo && draw_status.indexing) ? "Indexing files..." : "No matches");
        draw_utf8(d, w, gc, static_cast<int>(layout.results_viewport.x), static_cast<int>(layout.results_viewport.y + 28),
                  ellipsize_utf8(message, std::max(8, static_cast<int>(layout.results_viewport.width) - dip_px(state, 16.0f))));
    }

    if (such::ui::max_result_scroll(layout, state.results.size()) > 0.0f) {
        const float max_scroll = such::ui::max_result_scroll(layout, state.results.size());
        const float content = such::ui::result_content_height(layout, state.results.size());
        const float ratio = content > 0.0f ? layout.results_viewport.height / content : 1.0f;
        const int bar_h = std::max(24, static_cast<int>(layout.results_viewport.height * ratio));
        const float travel = std::max(1.0f, layout.results_viewport.height - static_cast<float>(bar_h));
        const int bar_y = static_cast<int>(layout.results_viewport.y + travel * (state.scroll_dip / max_scroll));
        set_color(d, gc, kHairline);
        XFillRectangle(d, w, gc,
                       static_cast<int>(layout.results_viewport.x + layout.results_viewport.width - 3.0f), bar_y,
                       2u, static_cast<unsigned>(bar_h));
    }

    if (!state.suggestions.empty()) {
        const int count = std::min(6, static_cast<int>(state.suggestions.size()));
        const int itemH = dip_px(state, 28.0f);
        const int px0 = static_cast<int>(sr.x);
        const int py0 = static_cast<int>(sr.y + sr.height + dip_px(state, 4.0f));
        const int pw = static_cast<int>(sr.width);
        fill_round_rect(d, w, gc, px0, py0, pw, itemH * count, dip_px(state, 5.0f), kBright);
        stroke_round_rect(d, w, gc, px0, py0, pw, itemH * count, dip_px(state, 5.0f), kHairline, dip_px(state, 1.0f));
        for (int i = 0; i < count; ++i) {
            set_color(d, gc, kDarkGreen);
            const auto& s = state.suggestions[static_cast<std::size_t>(i)];
            draw_utf8(d, w, gc, px0 + dip_px(state, 12.0f), py0 + i * itemH + dip_px(state, 19.0f), s.token);
            set_color(d, gc, kPathGreen);
            draw_utf8(d, w, gc, px0 + dip_px(state, 120.0f), py0 + i * itemH + dip_px(state, 19.0f), s.label);
        }
    }

    draw_security_notice(d, w, gc, state);
}


bool recreate_font_set(Display* d, std::string_view family, float scale) {
    if (gFontSet) {
        XFreeFontSet(d, gFontSet);
        gFontSet = nullptr;
    }
    std::vector<std::string> candidates;
    if (!family.empty()) {
        candidates.emplace_back(family);
        std::string xlfd = "-*-";
        for (char c : family) xlfd.push_back(c == ' ' ? '*' : c);
        xlfd += "-medium-r-normal--" + std::to_string(std::max(11, static_cast<int>(std::lround(14.0f * scale)))) + "-*-*-*-*-*-*-*";
        candidates.push_back(std::move(xlfd));
    }
    candidates.emplace_back("-*-*-medium-r-normal--" + std::to_string(std::max(11, static_cast<int>(std::lround(14.0f * scale)))) + "-*-*-*-*-*-*-*");
    for (const auto& pattern : candidates) {
        char** missing = nullptr;
        int missingCount = 0;
        char* defaultString = nullptr;
        gFontSet = XCreateFontSet(d, pattern.c_str(), &missing, &missingCount, &defaultString);
        if (missing) XFreeStringList(missing);
        if (gFontSet) return true;
    }
    return false;
}

bool execute_font_command_linux(Display* d, UiState& state) {
    const auto cmd = such::ui::parse_font_command(state.query, such::ui::PlatformDialect::UnixLike);
    if (!cmd.matched || cmd.show_picker || !cmd.requested_family.has_value()) return false;
    state.font_family = *cmd.requested_family;
    std::string error;
    const bool saved = state.font_family.empty() ? such::ui::clear_font_preference(&error)
                                                 : such::ui::save_font_preference(state.font_family, &error);
    if (!saved) std::fprintf(stderr, "Such: could not save font preference: %s\n", error.c_str());
    (void)recreate_font_set(d, state.font_family, state.ui_scale);
    state.query.clear();
    refresh_query(state);
    return true;
}


std::optional<std::filesystem::path> default_index_root_linux() {
    const char* home = std::getenv("HOME");
    std::filesystem::path candidate;
    if (home != nullptr && *home != '\0') {
        candidate = home;
    } else if (const passwd* pw = ::getpwuid(::getuid()); pw != nullptr && pw->pw_dir != nullptr) {
        candidate = pw->pw_dir;
    }

    if (candidate.empty() || candidate == std::filesystem::path("/")) return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec) || ec) return std::nullopt;
    return candidate;
}

bool bootstrap_default_index_root_linux(UiState& state) {
    std::string roots_error;
    const auto persisted = state.runtime.roots(&roots_error);
    if (!roots_error.empty()) {
        std::fprintf(stderr, "Such: could not read persisted search roots: %s\n", roots_error.c_str());
        return false;
    }

    std::vector<std::filesystem::path> valid_roots;
    valid_roots.reserve(persisted.size());
    for (const auto& value : persisted) {
        std::error_code ec;
        const std::filesystem::path path(value);
        if (std::filesystem::is_directory(path, ec) && !ec) valid_roots.push_back(path);
        else std::fprintf(stderr, "Such: stale search root will be removed: %s\n", value.c_str());
    }

    // Persisted user choices are authoritative. Repair the root set in one
    // mutation so startup never launches N asynchronous crawls for N stale roots.
    if (valid_roots.empty()) {
        const auto default_root = default_index_root_linux();
        if (default_root.has_value()) valid_roots.push_back(*default_root);
    }

    std::vector<std::string> desired_roots;
    desired_roots.reserve(valid_roots.size());
    for (const auto& root : valid_roots) desired_roots.push_back(root.string());
    const bool changed = desired_roots != persisted;
    if (!changed) return true;
    if (valid_roots.empty()) {
        std::string error;
        if (!state.runtime.replace_roots({}, &error)) {
            std::fprintf(stderr, "Such: could not clear invalid Linux search roots: %s\n", error.c_str());
            return false;
        }
        std::fprintf(stderr, "Such: no safe default Linux index root was found; use //drive and choose a mounted folder.\n");
        return false;
    }

    std::string error;
    if (!state.runtime.replace_roots(valid_roots, &error)) {
        std::fprintf(stderr, "Such: could not repair Linux search roots: %s\n", error.c_str());
        return false;
    }
    if (persisted.empty()) std::fprintf(stderr, "Such: active Linux index root: %s\n", valid_roots.front().string().c_str());
    return true;
}

bool execute_agent_command_linux(UiState& state) {
    const auto command = such::ui::parse_agent_command(state.query, such::ui::PlatformDialect::UnixLike);
    if (!command.matched) return false;
    // Agent sessions are rooted at the active Such search root. Selection is a
    // search result, not a workspace ownership boundary.
    std::string error;
    const auto roots = state.runtime.roots(&error);
    if (!error.empty()) {
        state.search_error = "Could not determine AI working directory: " + error;
        std::fprintf(stderr, "Such: %s\n", state.search_error.c_str());
        return true;
    }
    const auto working = such::ui::preferred_agent_working_directory(roots);
    if (!such::ui::launch_agent_terminal(command.kind, working, &error)) {
        std::fprintf(stderr, "Such: could not launch AI agent: %s\n", error.c_str());
    }
    state.query.clear();
    state.suggestions.clear();
    refresh_query(state);
    return true;
}

bool execute_index_command_linux(UiState& state) {
    const auto command = such::ui::parse_index_command(state.query, such::ui::PlatformDialect::UnixLike);
    if (!command.matched) return false;
    std::string error;
    switch (command.kind) {
        case such::ui::IndexCommandKind::AddRoot:
        case such::ui::IndexCommandKind::ReplaceRoot: {
            std::optional<std::filesystem::path> root;
            if (command.argument.has_value()) {
                root = std::filesystem::path(*command.argument);
            } else if (state.root_picker_kind == command.kind &&
                       state.selected >= 0 && state.selected < static_cast<int>(state.results.size())) {
                root = std::filesystem::path(state.results[static_cast<std::size_t>(state.selected)].path);
            } else {
                // Typing //drive or //index opens the built-in mounted-volume
                // picker. Require an explicit arrow/click selection before Enter.
                refresh_query(state, false);
                return true;
            }

            std::error_code ec;
            if (!std::filesystem::is_directory(*root, ec) || ec) {
                state.suggestions = {{state.query, "Folder is not mounted or is not accessible"}};
                return true;
            }

            const bool ok = command.kind == such::ui::IndexCommandKind::AddRoot
                ? state.runtime.add_root(*root, &error)
                : state.runtime.replace_roots({*root}, &error);
            if (!ok) {
                state.suggestions = {{state.query, error.empty() ? "Could not use this search folder" : error}};
                return true;
            }

            // add_root/replace_roots already schedule the private runtime's
            // asynchronous crawl. Do NOT call reindex_async() here: doing so
            // restarts a whole-drive crawl and can make a real mounted volume
            // appear permanently empty while two scans run back-to-back.
            state.runtime_generation = state.runtime.generation();
            state.query.clear();
            state.results.clear();
            state.suggestions.clear();
            state.root_picker_kind = such::ui::IndexCommandKind::NoCommand;
            state.selected = -1;
            state.scroll_dip = 0.0f;
            return true;
        }
        case such::ui::IndexCommandKind::Reindex:
            state.runtime.reindex_async();
            state.query.clear();
            state.results.clear();
            state.suggestions.clear();
            state.scroll_dip = 0.0f;
            return true;
        case such::ui::IndexCommandKind::ShowRoots:
            refresh_query(state);
            return true;
        case such::ui::IndexCommandKind::NoCommand:
            return false;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (such::cli::should_dispatch_from_gui(argc, argv)) {
        return such::cli::run(argc, argv, such::ui::PlatformDialect::UnixLike);
    }
    std::signal(SIGCHLD, SIG_IGN);
    UiState state;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke") == 0) state.smoke = true;
        if (std::strcmp(argv[i], "--demo") == 0) state.demo = true;
    }
    if (state.demo) refresh_query(state);

    auto lease = such::ui::FrontendLease::try_acquire(such::ui::FrontendMode::Gui);
    if (!lease.acquired()) {
        std::fprintf(stderr, "Such GUI unavailable: %s\n", lease.error().c_str());
        return 23;
    }


    std::string runtime_error;
    if (!state.demo && !state.runtime.load(&runtime_error)) {
        std::fprintf(stderr, "Such: index load failed: %s\n", runtime_error.c_str());
        // A production launch without a ready runtime is a broken product, not
        // a reduced-function GUI. Fail closed instead of presenting a search box
        // that can never return a result.
        return 5;
    }
    if (!state.demo && !state.smoke && state.runtime.available()) {
        // Validate persisted roots on every normal launch. Fresh installs get
        // HOME; stale roots from older builds/removable mounts are pruned and
        // HOME is restored only when no valid user root remains.
        (void)bootstrap_default_index_root_linux(state);
    }
    if (!state.demo) {
        const auto status = state.runtime.status();
        state.runtime_generation = status.generation;
        state.runtime_indexed_files = status.indexed_files;
        state.runtime_indexing = status.indexing;
        // A loaded runtime may already be restoring/rebuilding its corpus. Only
        // request an explicit rebuild when it is genuinely idle and empty.
        if (!status.indexing && status.indexed_files == 0 && !status.roots.empty()) state.runtime.reindex_async();
    }

    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        std::fprintf(stderr, "Such: XOpenDisplay failed\n");
        return 2;
    }
    std::setlocale(LC_CTYPE, "");
    XSetLocaleModifiers("");
    if (const auto preferred = such::ui::load_font_preference(); preferred) state.font_family = *preferred;

    const int screen = DefaultScreen(d);
    state.ui_scale = detect_ui_scale(d, screen);
    state.width = static_cast<int>(std::lround(760.0f * state.ui_scale));
    state.height = static_cast<int>(std::lround(600.0f * state.ui_scale));
    (void)recreate_font_set(d, state.font_family, state.ui_scale);
    Window w = XCreateSimpleWindow(d, RootWindow(d, screen), 60, 60, static_cast<unsigned>(state.width), static_cast<unsigned>(state.height),
                                   1, kDarkGreen, kPaper);
    XStoreName(d, w, such::version::kDisplayName.data());
    XSizeHints hints{};
    hints.flags = PMinSize;
    hints.min_width = dip_px(state, 420.0f);
    hints.min_height = dip_px(state, 390.0f);
    XSetWMNormalHints(d, w, &hints);
    XSelectInput(d, w, ExposureMask | StructureNotifyMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask);
    Atom wmDelete = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, w, &wmDelete, 1);
    gInputMethod = XOpenIM(d, nullptr, nullptr, nullptr);
    if (gInputMethod) {
        gInputContext = XCreateIC(gInputMethod,
            XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
            XNClientWindow, w,
            XNFocusWindow, w,
            nullptr);
        // XIC retains its XIM relationship. It is destroyed before the display closes.
    }
    XMapWindow(d, w);
    GC gc = XCreateGC(d, w, 0, nullptr);

    bool running = true;
    int exposes = 0;
    while (running) {
        if (XPending(d) == 0) {
            pollfd fd{};
            fd.fd = ConnectionNumber(d);
            fd.events = POLLIN;
            int poll_result = 0;
            do {
                poll_result = ::poll(&fd, 1, 250);
            } while (poll_result < 0 && errno == EINTR);
            if (poll_result < 0) {
                std::fprintf(stderr, "Such: X11 event poll failed.\n");
                break;
            }

            // Indexing is asynchronous. The old Linux loop blocked forever in
            // XNextEvent(), so a query entered while indexing stayed empty until
            // another key/mouse event happened. Poll the runtime generation on
            // the same cadence as Windows and refresh only an active query.
            if (!state.demo && state.runtime.available()) {
                const auto status = state.runtime.status();
                const bool content_changed = status.generation != state.runtime_generation ||
                                             status.indexed_files != state.runtime_indexed_files;
                const bool activity_changed = status.indexing != state.runtime_indexing;
                if (content_changed || activity_changed) {
                    state.runtime_generation = status.generation;
                    state.runtime_indexed_files = status.indexed_files;
                    state.runtime_indexing = status.indexing;
                    if (!state.query.empty() && content_changed) refresh_query(state, false);
                    draw(d, w, gc, state);
                }
            }
            if (XPending(d) == 0) continue;
        }

        XEvent e{};
        XNextEvent(d, &e);
        switch (e.type) {
            case ConfigureNotify:
                state.width = e.xconfigure.width;
                state.height = e.xconfigure.height;
                state.swipe.close();
                draw(d, w, gc, state);
                break;
            case Expose:
                if (e.xexpose.count == 0) {
                    draw(d, w, gc, state);
                    if (state.smoke && ++exposes >= 1) running = false;
                }
                break;
            case KeyPress: {
                char buf[256]{};
                KeySym ks{};
                int n = 0;
                if (gInputContext) {
                    Status status{};
                    n = Xutf8LookupString(gInputContext, &e.xkey, buf, static_cast<int>(sizeof(buf) - 1), &ks, &status);
                    if (n < 0) n = 0;
                } else {
                    n = XLookupString(&e.xkey, buf, static_cast<int>(sizeof(buf) - 1), &ks, nullptr);
                }
                if (ks == XK_Escape) {
                    if (state.security_notice_open) {
                        state.security_notice_open = false;
                    } else if (state.swipe.is_open() || !state.suggestions.empty()) {
                        state.swipe.close();
                        state.suggestions.clear();
                    } else {
                        running = false;
                    }
                } else if (ks == XK_BackSpace && !state.query.empty()) {
                    // XIM returns UTF-8. Delete one code point rather than one byte so
                    // Korean/CJK input cannot leave an invalid query buffer.
                    pop_utf8_codepoint(state.query);
                    refresh_query(state);
                } else if (ks == XK_Tab) {
                    complete_first(state);
                } else if (ks == XK_Down && !state.results.empty()) {
                    state.selected = std::min(state.selected + 1, static_cast<int>(state.results.size()) - 1);
                    ensure_selected_visible(state);
                } else if (ks == XK_Up && !state.results.empty()) {
                    state.selected = state.selected <= 0 ? 0 : state.selected - 1;
                    ensure_selected_visible(state);
                } else if ((ks == XK_Return || ks == XK_KP_Enter) && execute_agent_command_linux(state)) {
                    // /claude or /codex consumed.
                } else if ((ks == XK_Return || ks == XK_KP_Enter) && execute_font_command_linux(d, state)) {
                    // Hidden //font command consumed.
                } else if ((ks == XK_Return || ks == XK_KP_Enter) && execute_index_command_linux(state)) {
                    // Hidden //index / //reindex / //roots command consumed.
                } else if ((ks == XK_Return || ks == XK_KP_Enter) &&
                           state.selected >= 0 && state.selected < static_cast<int>(state.results.size())) {
                    open_item_linux(state.results[static_cast<std::size_t>(state.selected)]);
                } else if ((e.xkey.state & Mod1Mask) != 0 &&
                           state.selected >= 0 && state.selected < static_cast<int>(state.results.size()) &&
                           (ks == XK_l || ks == XK_L)) {
                    perform_action(state, state.selected, such::ui::ResultAction::OpenLocation);
                } else if ((e.xkey.state & Mod1Mask) != 0 &&
                           state.selected >= 0 && state.selected < static_cast<int>(state.results.size()) &&
                           (ks == XK_i || ks == XK_I)) {
                    perform_action(state, state.selected, such::ui::ResultAction::ToggleIndex);
                } else if ((e.xkey.state & Mod1Mask) != 0 &&
                           state.selected >= 0 && state.selected < static_cast<int>(state.results.size()) &&
                           (ks == XK_p || ks == XK_P)) {
                    perform_action(state, state.selected, such::ui::ResultAction::TogglePin);
                } else if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20) {
                    state.query.append(buf, buf + n);
                    refresh_query(state);
                }
                draw(d, w, gc, state);
                break;
            }
            case ButtonPress: {
                if (e.xbutton.button == Button4 || e.xbutton.button == Button5) {
                    const auto layout = x11_layout(state);
                    const float step = (layout.metrics.row_height + layout.metrics.row_gap) * 2.5f;
                    state.scroll_dip += e.xbutton.button == Button4 ? -step : step;
                    state.scroll_dip = std::clamp(state.scroll_dip, 0.0f, such::ui::max_result_scroll(layout, state.results.size()));
                    state.swipe.close();
                    draw(d, w, gc, state);
                    break;
                }
                if (e.xbutton.button != Button1) break;
                if (state.security_notice_open) {
                    if (point_in_rect(security_notice_contact_button(state), e.xbutton.x, e.xbutton.y)) {
                        open_enterprise_url_linux();
                        state.security_notice_open = false;
                    } else if (point_in_rect(security_notice_cancel_button(state), e.xbutton.x, e.xbutton.y)) {
                        state.security_notice_open = false;
                    }
                    draw(d, w, gc, state);
                    break;
                }
                const auto security_layout = x11_layout(state);
                if (point_in_rect(security_layout.security, e.xbutton.x, e.xbutton.y)) {
                    request_security_toggle(state);
                    state.swipe.close();
                    draw(d, w, gc, state);
                    break;
                }
                int action_row = -1;
                such::ui::ResultAction action{};
                if (hit_swipe_action(state, e.xbutton.x, e.xbutton.y, action_row, action)) {
                    perform_action(state, action_row, action);
                    draw(d, w, gc, state);
                    break;
                }
                const int row = hit_row(state, e.xbutton.x, e.xbutton.y);
                if (row >= 0) {
                    state.selected = row;
                    state.swipe.set_action_tray_width(such::ui::compute_responsive_metrics(
                        static_cast<float>(state.width), static_cast<float>(state.height)).swipe_action_width * 3.0f);
                    state.swipe.begin(row, static_cast<float>(e.xbutton.x) / state.ui_scale, static_cast<float>(e.xbutton.y) / state.ui_scale);
                    state.last_motion = std::chrono::steady_clock::now();
                } else {
                    state.swipe.close();
                }
                draw(d, w, gc, state);
                break;
            }
            case MotionNotify: {
                if (!state.swipe.state().dragging) break;
                const auto now = std::chrono::steady_clock::now();
                const float dt = std::max(0.001f, std::chrono::duration<float>(now - state.last_motion).count());
                state.last_motion = now;
                state.swipe.update(static_cast<float>(e.xmotion.x) / state.ui_scale, static_cast<float>(e.xmotion.y) / state.ui_scale, dt);
                draw(d, w, gc, state);
                break;
            }
            case ButtonRelease: {
                if (e.xbutton.button != Button1 || !state.swipe.state().dragging) break;
                const int row = state.swipe.state().row;
                if (const auto action = state.swipe.end(static_cast<float>(state.width) / state.ui_scale)) {
                    perform_action(state, row, *action);
                }
                draw(d, w, gc, state);
                break;
            }
            case FocusIn:
                if (gInputContext) XSetICFocus(gInputContext);
                break;
            case FocusOut:
                if (gInputContext) XUnsetICFocus(gInputContext);
                state.swipe.close();
                break;
            case ClientMessage:
                if (static_cast<Atom>(e.xclient.data.l[0]) == wmDelete) running = false;
                break;
            default:
                break;
        }
    }

    if (gInputContext) { XDestroyIC(gInputContext); gInputContext = nullptr; }
    if (gInputMethod) { XCloseIM(gInputMethod); gInputMethod = nullptr; }
    if (gFontSet) { XFreeFontSet(d, gFontSet); gFontSet = nullptr; }
    XFreeGC(d, gc);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 0;
}

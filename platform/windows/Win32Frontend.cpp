#include <such/Version.h>
#include <such/runtime/RuntimeClient.h>
#include <such/security/SecureSearchGate.h>
#include <such/ui/DesignContract.h>
#include <such/ui/AgentLauncher.h>
#include <such/ui/IndexCommands.h>
#include <such/ui/FrontendLease.h>
#include <such/ui/FontSettings.h>
#include <such/ui/ResponsiveLayout.h>
#include <such/ui/ResultView.h>
#include <such/ui/SearchDialect.h>
#include <such/ui/SwipeActions.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using such::ui::ResultAction;
using such::ui::SwipeSide;

constexpr wchar_t kClassName[] = L"HeritageSuchV110Window";
constexpr wchar_t kSearchClass[] = L"EDIT";
constexpr UINT kMsgAutocomplete = WM_APP + 1;
constexpr UINT kMsgCloseTransient = WM_APP + 2;
constexpr UINT kMsgOpenSelected = WM_APP + 3;
constexpr UINT kMsgEnsureIndexRoot = WM_APP + 4;
constexpr UINT_PTR kTimerRuntimeRefresh = 3;

constexpr float kSearchLeftContentInsetDip = 39.0f;
constexpr float kSearchRightContentInsetDip = 44.0f;

constexpr COLORREF kPaper = RGB(243, 239, 229);       // #F3EFE5
constexpr COLORREF kBright = RGB(251, 248, 239);      // #FBF8EF
constexpr COLORREF kDarkGreen = RGB(23, 58, 43);      // #173A2B
constexpr COLORREF kPathGreen = RGB(84, 103, 91);
constexpr COLORREF kPlaceholderGray = RGB(184, 181, 173);
constexpr COLORREF kHairline = RGB(211, 205, 191);
constexpr COLORREF kHighlight = RGB(255, 253, 247);
constexpr COLORREF kAction2 = RGB(34, 76, 58);
constexpr COLORREF kAction3 = RGB(47, 91, 70);
constexpr COLORREF kSandAccent = RGB(176, 142, 86);
constexpr COLORREF kBlueAccent = RGB(75, 110, 129);
constexpr COLORREF kRoseAccent = RGB(145, 91, 91);

constexpr int kSearchId = 1001;
constexpr int kMenuLocation = 3001;
constexpr int kMenuIndex = 3002;
constexpr int kMenuPin = 3003;
constexpr int kMenuProperties = 3004;
constexpr int kMenuAccentNone = 3100;
constexpr int kMenuAccentGreen = 3101;
constexpr int kMenuAccentSand = 3102;
constexpr int kMenuAccentBlue = 3103;
constexpr int kMenuAccentRose = 3104;
constexpr int kMenuIconSystem = 3200;
constexpr int kMenuIconDocument = 3201;
constexpr int kMenuIconFolder = 3202;
constexpr int kMenuIconFavorite = 3203;

HWND gSearch = nullptr;
WNDPROC gOldSearchProc = nullptr;
HFONT gSearchFont = nullptr;
HFONT gFilenameFont = nullptr;
HFONT gPathFont = nullptr;
HFONT gBadgeFont = nullptr;
UINT gFontDpi = 0;
std::array<int, 3> gFontHeights{};
std::wstring gFontFamily = L"Segoe UI";
int gFontPickerSelection = 0;
HBRUSH gSearchBrush = nullptr;
std::unordered_map<std::wstring, HICON> gIconCache;
std::wstring gQuery;
std::vector<such::ui::ResultItem> gResults;
std::string gSearchError;
std::unique_ptr<such::runtime::RuntimeClient> gRuntime;
such::security::SecureSearchGate gSecurityGate;
std::uint64_t gRuntimeGeneration = 0;
std::size_t gRuntimeIndexedFiles = 0;
bool gRuntimeIndexing = false;
float gScrollDip = 0.0f;
std::vector<such::ui::Suggestion> gSuggestions;
bool gDemo = false;
bool gSmoke = false;
bool gSmokeHold = false;
int gSelectedRow = -1;
int gHoverRow = -1;
ULONGLONG gLastPointerTick = 0;
UINT32 gActivePointerId = 0;
ULONGLONG gIgnoreMouseUntil = 0;
#if defined(SUCH_EXPERIMENTAL_LEADING_SWIPE)
constexpr bool kAllowLeadingSwipe = true;
#else
constexpr bool kAllowLeadingSwipe = false;
#endif
such::ui::SwipeController gSwipe(216.0f, kAllowLeadingSwipe);

struct ScopedComApartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ~ScopedComApartment() {
        if (SUCCEEDED(result)) CoUninitialize();
    }
};

float scale_for_window(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return static_cast<float>(dpi) / 96.0f;
}

int px(float dip, float scale) {
    return static_cast<int>(std::lround(dip * scale));
}

std::wstring widen_utf8(const std::string& value) {
    if (value.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), n);
    return out;
}

std::string narrow_utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// Search-detail layout belongs to the frontend layout layer, not the query
// engine. Reserve one compact branch row per /; refinement so rendering, hit
// testing and scrolling all share the same geometry.
such::ui::WindowLayout window_layout_for_query(float width_dip, float height_dip) {
    auto layout = such::ui::compute_window_layout(width_dip, height_dip);
    const auto detail = such::ui::parse_detail_search(narrow_utf8(gQuery));
    if (!detail.active || detail.refinements.empty()) return layout;

    const float branch_h = std::clamp(layout.metrics.row_height * 0.50f, 28.0f, 36.0f);
    const float branch_gap = std::clamp(layout.metrics.row_gap * 0.75f, 4.0f, 7.0f);
    const float extra = static_cast<float>(detail.refinements.size()) * branch_h
                      + static_cast<float>(detail.refinements.size()) * branch_gap;
    layout.results_viewport.y += extra;
    layout.results_viewport.height = std::max(0.0f, layout.results_viewport.height - extra);
    const float stride = layout.metrics.row_height + layout.metrics.row_gap;
    layout.metrics.visible_results = stride > 0.0f
        ? std::max(0, static_cast<int>(std::floor((layout.results_viewport.height + layout.metrics.row_gap) / stride)))
        : 0;
    for (std::size_t i = 0; i < layout.rows.size(); ++i)
        layout.rows[i] = such::ui::result_row_rect(layout, i, 0.0f);
    return layout;
}


void delete_font(HFONT& font) {
    if (font) {
        DeleteObject(font);
        font = nullptr;
    }
}

HFONT make_font(float points, UINT dpi, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(static_cast<int>(std::lround(points * 10.0f)), static_cast<int>(dpi), 720);
    lf.lfWeight = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, gFontFamily.c_str(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

int font_pixel_height(float points, UINT dpi) {
    return -MulDiv(static_cast<int>(std::lround(points * 10.0f)), static_cast<int>(dpi), 720);
}

void ensure_fonts(HWND hwnd, const such::ui::ResponsiveMetrics& m) {
    const UINT dpi = GetDpiForWindow(hwnd);
    const std::array<int, 3> desired{{
        font_pixel_height(m.filename_font, dpi),
        font_pixel_height(m.path_font, dpi),
        font_pixel_height(std::max(8.0f, m.path_font - 1.5f), dpi),
    }};
    if (dpi == gFontDpi && desired == gFontHeights && gSearchFont && gFilenameFont &&
        gPathFont && gBadgeFont) {
        return;
    }
    // Create replacements first. The EDIT control retains the HFONT supplied by
    // WM_SETFONT, so deleting the old search font before swapping the control to a
    // new one leaves a short-lived dangling GDI handle during resize/DPI changes.
    HFONT newSearch = make_font(m.filename_font, dpi, FW_NORMAL);
    HFONT newFilename = make_font(m.filename_font, dpi, FW_SEMIBOLD);
    HFONT newPath = make_font(m.path_font, dpi, FW_NORMAL);
    HFONT newBadge = make_font(std::max(8.0f, m.path_font - 1.5f), dpi, FW_BOLD);

    if (gSearch && newSearch) {
        SendMessageW(gSearch, WM_SETFONT, reinterpret_cast<WPARAM>(newSearch), TRUE);
    }

    delete_font(gSearchFont);
    delete_font(gFilenameFont);
    delete_font(gPathFont);
    delete_font(gBadgeFont);

    gSearchFont = newSearch;
    gFilenameFont = newFilename;
    gPathFont = newPath;
    gBadgeFont = newBadge;
    gFontDpi = dpi;
    gFontHeights = desired;
}

RECT to_rect(const such::ui::RectF& r, float scale, int dx = 0) {
    return RECT{
        px(r.x, scale) + dx,
        px(r.y, scale),
        px(r.x + r.width, scale) + dx,
        px(r.y + r.height, scale),
    };
}

RECT search_clear_rect(const RECT& search, float scale) {
    const int size = px(26.0f, scale);
    const int right = static_cast<int>(search.right) - px(10.0f, scale);
    const int cy = static_cast<int>((search.top + search.bottom) / 2);
    return RECT{right - size, cy - size / 2, right, cy + size / 2};
}

bool point_in_rect(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

bool pointer_client_point(HWND hwnd, WPARAM wp, POINT& client, UINT32& pointerId) {
    pointerId = GET_POINTERID_WPARAM(wp);
    POINTER_INFO info{};
    if (!GetPointerInfo(pointerId, &info)) return false;
    client = info.ptPixelLocation;
    return ScreenToClient(hwnd, &client) != FALSE;
}

void fill_round(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    const HGDIOBJ oldBrush = SelectObject(dc, brush);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void stroke_round(HDC dc, const RECT& rect, int radius, COLORREF color, int width = 1) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void draw_search_glyph(HDC dc, int cx, int cy, int size, COLORREF color = kDarkGreen) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 8), color);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
    const int r = size / 3;
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    MoveToEx(dc, cx + r - 1, cy + r - 1, nullptr);
    LineTo(dc, cx + size / 2, cy + size / 2);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void draw_pin_mark(HDC dc, int x, int y, int size) {
    HPEN pen = CreatePen(PS_SOLID, std::max(1, size / 7), kDarkGreen);
    HBRUSH brush = CreateSolidBrush(kDarkGreen);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    const HGDIOBJ oldBrush = SelectObject(dc, brush);
    Ellipse(dc, x, y, x + size, y + size / 2);
    MoveToEx(dc, x + size / 2, y + size / 3, nullptr);
    LineTo(dc, x + size / 2, y + size);
    MoveToEx(dc, x + size / 2, y + size, nullptr);
    LineTo(dc, x + size / 3, y + size + size / 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void draw_clear_button(HDC dc, const RECT& rect, bool hot) {
    const int diameter = static_cast<int>(rect.right - rect.left);
    const int radius = std::max(1, diameter / 2);
    fill_round(dc, rect, radius, hot ? RGB(232, 229, 220) : RGB(239, 236, 227));
    HPEN pen = CreatePen(PS_SOLID, std::max(1, diameter / 12), kDarkGreen);
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    const int pad = std::max(5, diameter / 3);
    MoveToEx(dc, rect.left + pad, rect.top + pad, nullptr);
    LineTo(dc, rect.right - pad, rect.bottom - pad);
    MoveToEx(dc, rect.right - pad, rect.top + pad, nullptr);
    LineTo(dc, rect.left + pad, rect.bottom - pad);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void draw_action_glyph(HDC dc, ResultAction action, const RECT& actionRect, float scale) {
    const int size = px(18.0f, scale);
    const int cx = static_cast<int>((actionRect.left + actionRect.right) / 2);
    const int top = static_cast<int>(actionRect.top) + px(10.0f, scale);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, px(1.5f, scale)), RGB(255, 255, 255));
    HBRUSH hollow = reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    const HGDIOBJ oldPen = SelectObject(dc, pen);
    const HGDIOBJ oldBrush = SelectObject(dc, hollow);

    switch (action) {
        case ResultAction::OpenLocation: {
            RECT folder{cx - size / 2, top + size / 4, cx + size / 2, top + size};
            Rectangle(dc, folder.left, folder.top, folder.right, folder.bottom);
            MoveToEx(dc, folder.left + size / 8, folder.top, nullptr);
            LineTo(dc, folder.left + size / 3, folder.top - size / 5);
            LineTo(dc, folder.left + size / 2, folder.top);
            break;
        }
        case ResultAction::ToggleIndex: {
            const int rx = size / 2;
            const int ry = std::max(2, size / 6);
            const int y0 = top + size / 5;
            Ellipse(dc, cx - rx, y0 - ry, cx + rx, y0 + ry);
            MoveToEx(dc, cx - rx, y0, nullptr); LineTo(dc, cx - rx, y0 + size * 3 / 4);
            MoveToEx(dc, cx + rx, y0, nullptr); LineTo(dc, cx + rx, y0 + size * 3 / 4);
            Arc(dc, cx - rx, y0 + size / 2, cx + rx, y0 + size, 0, 0, 0, 0);
            break;
        }
        case ResultAction::TogglePin: {
            const int x = cx - size / 3;
            MoveToEx(dc, x, top + size / 4, nullptr);
            LineTo(dc, x + size * 2 / 3, top + size / 4);
            MoveToEx(dc, cx, top + size / 4, nullptr);
            LineTo(dc, cx, top + size);
            MoveToEx(dc, cx, top + size, nullptr);
            LineTo(dc, cx - size / 5, top + size + size / 4);
            break;
        }
        case ResultAction::OpenProperties: {
            Rectangle(dc, cx - size / 2, top, cx + size / 2, top + size);
            for (int i = 0; i < 3; ++i) {
                const int y = top + size / 4 + i * size / 4;
                MoveToEx(dc, cx - size / 4, y, nullptr);
                LineTo(dc, cx + size / 4, y);
            }
            break;
        }
        case ResultAction::EditAppearance: {
            Ellipse(dc, cx - size / 2, top, cx + size / 2, top + size);
            Ellipse(dc, cx - size / 5, top + size / 4, cx + size / 6, top + size * 3 / 5);
            break;
        }
    }

    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

COLORREF accent_color(such::ui::AccentTone tone) {
    switch (tone) {
        case such::ui::AccentTone::Green: return kDarkGreen;
        case such::ui::AccentTone::Sand: return kSandAccent;
        case such::ui::AccentTone::Blue: return kBlueAccent;
        case such::ui::AccentTone::Rose: return kRoseAccent;
        default: return CLR_INVALID;
    }
}

HICON load_stock_icon(such::ui::IconOverride kind, int size) {
    SHSTOCKICONID id = SIID_DOCNOASSOC;
    switch (kind) {
        case such::ui::IconOverride::Folder: id = SIID_FOLDER; break;
        case such::ui::IconOverride::Favorite: id = SIID_LINK; break;
        case such::ui::IconOverride::Document: id = SIID_DOCNOASSOC; break;
        default: return nullptr;
    }
    SHSTOCKICONINFO sii{};
    sii.cbSize = static_cast<DWORD>(sizeof(sii));
    const UINT flags = SHGSI_ICON | (size <= 20 ? SHGSI_SMALLICON : SHGSI_LARGEICON);
    if (SUCCEEDED(SHGetStockIconInfo(id, flags, &sii))) return sii.hIcon;
    return nullptr;
}

std::wstring icon_cache_key(const such::ui::ResultItem& item, int size) {
    std::wstring key = widen_utf8(item.path);
    key.push_back(L'\x1f');
    key += std::to_wstring(size);
    key.push_back(L':');
    key += std::to_wstring(static_cast<unsigned int>(item.icon_override));
    return key;
}

HICON native_icon_cached(const such::ui::ResultItem& item, int size) {
    const std::wstring key = icon_cache_key(item, size);
    if (const auto it = gIconCache.find(key); it != gIconCache.end()) return it->second;

    HICON icon = nullptr;
    if (item.icon_override != such::ui::IconOverride::SystemDefault) {
        icon = load_stock_icon(item.icon_override, size);
    } else {
        const std::wstring path = widen_utf8(item.path);
        SHFILEINFOW info{};
        UINT flags = SHGFI_ICON | (size <= 20 ? SHGFI_SMALLICON : SHGFI_LARGEICON);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) flags |= SHGFI_USEFILEATTRIBUTES;
        if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, static_cast<UINT>(sizeof(info)), flags) != 0) {
            icon = info.hIcon;
        }
    }
    if (!icon) icon = load_stock_icon(such::ui::IconOverride::Document, size);
    if (icon) gIconCache.emplace(key, icon);
    return icon;
}

void clear_icon_cache() noexcept {
    for (const auto& entry : gIconCache) {
        if (entry.second) DestroyIcon(entry.second);
    }
    gIconCache.clear();
}

void draw_extension_badge(HDC dc, const std::wstring& extension, int x, int y, int height) {
    if (extension.empty()) return;
    std::wstring upper = extension;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(static_cast<wint_t>(c))); });
    if (upper.size() > 5) upper.resize(5);

    const HGDIOBJ oldFont = SelectObject(dc, gBadgeFont);
    SIZE s{};
    GetTextExtentPoint32W(dc, upper.c_str(), static_cast<int>(upper.size()), &s);
    const int width = s.cx + 10;
    RECT badge{x - width, y - height, x, y};
    fill_round(dc, badge, 3, kDarkGreen);
    stroke_round(dc, badge, 3, RGB(255, 255, 255), 1);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, upper.c_str(), static_cast<int>(upper.size()), &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void draw_action_tray(HDC dc, const RECT& row, SwipeSide side, float scale,
                      const such::ui::ResponsiveMetrics& m, const such::ui::ResultItem& item) {
    if (side == SwipeSide::Inactive) return;
    const auto actions = such::ui::actions_for_side(side);
    const int actionWidth = px(m.swipe_action_width, scale);
    const int total = actionWidth * 3;
    const int x0 = side == SwipeSide::Trailing
        ? static_cast<int>(row.right) - total
        : static_cast<int>(row.left);
    const std::array<COLORREF, 3> colors{kDarkGreen, kAction2, kAction3};
    const int radius = px(m.corner_radius, scale);
    RECT tray{x0, row.top, x0 + total, row.bottom};
    fill_round(dc, tray, radius, kDarkGreen);

    const HGDIOBJ oldFont = SelectObject(dc, gPathFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    for (int i = 0; i < 3; ++i) {
        const auto action = actions[static_cast<std::size_t>(i)];
        RECT a{x0 + i * actionWidth, row.top, x0 + (i + 1) * actionWidth, row.bottom};
        HBRUSH brush = CreateSolidBrush(colors[static_cast<std::size_t>(i)]);
        FillRect(dc, &a, brush);
        DeleteObject(brush);

        draw_action_glyph(dc, action, a, scale);
        const wchar_t* label = L"";
        switch (action) {
            case ResultAction::OpenLocation: label = L"Location"; break;
            case ResultAction::ToggleIndex: label = item.indexed ? L"Unindex" : L"Index"; break;
            case ResultAction::TogglePin: label = item.pinned ? L"Unpin" : L"Pin"; break;
            case ResultAction::OpenProperties: label = L"Properties"; break;
            case ResultAction::EditAppearance: label = L"Appearance"; break;
        }
        RECT labelRect = a;
        labelRect.top += px(27.0f, scale);
        DrawTextW(dc, label, -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (i != 0) {
            HPEN separator = CreatePen(PS_SOLID, 1, RGB(82, 116, 99));
            const HGDIOBJ oldPen = SelectObject(dc, separator);
            MoveToEx(dc, a.left, a.top + px(6.0f, scale), nullptr);
            LineTo(dc, a.left, a.bottom - px(6.0f, scale));
            SelectObject(dc, oldPen);
            DeleteObject(separator);
        }
    }
    SelectObject(dc, oldFont);
}

void draw_result_row(HDC dc, const such::ui::ResultItem& item, RECT row, int index, int offsetPx,
                     float scale, const such::ui::ResponsiveMetrics& m, bool selected, bool hovered) {
    const int radius = px(m.corner_radius, scale);

    if (gSwipe.state().row == index && gSwipe.state().side != SwipeSide::Inactive) {
        draw_action_tray(dc, row, gSwipe.state().side, scale, m, item);
    }

    OffsetRect(&row, offsetPx, 0);
    RECT shadow = row;
    OffsetRect(&shadow, 0, px(1.5f, scale));
    fill_round(dc, shadow, radius, RGB(225, 220, 208));
    fill_round(dc, row, radius, kBright);
    const COLORREF border = selected ? kDarkGreen : (hovered ? RGB(174, 185, 176) : kHairline);
    stroke_round(dc, row, radius, border, selected ? std::max(1, px(1.2f, scale)) : 1);

    // Very shallow emboss: one highlight hairline inside the top edge.
    HPEN hi = CreatePen(PS_SOLID, 1, kHighlight);
    HGDIOBJ oldPen = SelectObject(dc, hi);
    MoveToEx(dc, row.left + radius, row.top + 1, nullptr);
    LineTo(dc, row.right - radius, row.top + 1);
    SelectObject(dc, oldPen);
    DeleteObject(hi);

    if (const COLORREF accent = accent_color(item.accent); accent != CLR_INVALID) {
        RECT stripe{row.left + 1, row.top + radius, row.left + px(3.0f, scale), row.bottom - radius};
        HBRUSH b = CreateSolidBrush(accent);
        FillRect(dc, &stripe, b);
        DeleteObject(b);
    }

    const int inset = px(12.0f, scale);
    const int iconSize = px(m.file_icon, scale);
    const int iconX = static_cast<int>(row.left) + inset;
    const int iconY = static_cast<int>(row.top) +
        (static_cast<int>(row.bottom - row.top) - iconSize) / 2;
    if (HICON icon = native_icon_cached(item, iconSize)) {
        DrawIconEx(dc, iconX, iconY, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }
    draw_extension_badge(dc, widen_utf8(item.extension), iconX + iconSize + px(4.0f, scale), iconY + iconSize + px(2.0f, scale), px(m.extension_badge_height, scale));

    const int textX = iconX + iconSize + px(m.icon_gap, scale);
    const int pinReserve = item.pinned ? px(28.0f, scale) : 0;
    RECT filenameRect{textX, row.top + px(8.0f, scale), row.right - inset - pinReserve, row.top + px(31.0f, scale)};
    RECT pathRect{textX, row.top + px(31.0f, scale), row.right - inset, row.bottom - px(5.0f, scale)};

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kDarkGreen);
    HGDIOBJ oldFont = SelectObject(dc, gFilenameFont);
    const std::wstring filename = widen_utf8(item.filename);
    DrawTextW(dc, filename.c_str(), static_cast<int>(filename.size()), &filenameRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    SetTextColor(dc, kPathGreen);
    SelectObject(dc, gPathFont);
    const std::wstring path = widen_utf8(item.path);
    DrawTextW(dc, path.c_str(), static_cast<int>(path.size()), &pathRect,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (item.pinned) {
        draw_pin_mark(dc, row.right - inset - px(13.0f, scale), row.top + px(12.0f, scale), px(9.0f, scale));
    }
    SelectObject(dc, oldFont);
}



int CALLBACK enum_font_callback(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM param) {
    auto* found = reinterpret_cast<bool*>(param);
    *found = true;
    return 0;
}

bool font_family_available(HWND hwnd, const std::wstring& family) {
    if (family.empty() || family == L"Segoe UI") return true;
    HDC dc = GetDC(hwnd);
    if (!dc) return false;
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, family.c_str(), _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, enum_font_callback, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(hwnd, dc);
    return found;
}

void reset_font_handles(HWND hwnd) {
    gFontDpi = 0;
    gFontHeights = {};
    if (hwnd) {
        RECT client{};
        GetClientRect(hwnd, &client);
        const float scale = scale_for_window(hwnd);
        const auto layout = window_layout_for_query(
            static_cast<float>(client.right) / scale,
            static_cast<float>(client.bottom) / scale);
        ensure_fonts(hwnd, layout.metrics);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

bool apply_font_family(HWND hwnd, const std::string& familyUtf8, bool persist, bool showError) {
    std::wstring family = familyUtf8.empty() ? L"Segoe UI" : widen_utf8(familyUtf8);
    if (!font_family_available(hwnd, family)) {
        if (showError) {
            const std::wstring message = L"Font is not installed: " + family +
                L"\n\nInstall the font for this user, then run /font again.";
            MessageBoxW(hwnd, message.c_str(), L"Such font settings", MB_OK | MB_ICONINFORMATION);
        }
        return false;
    }
    gFontFamily = family;
    if (persist) {
        std::string error;
        const bool ok = familyUtf8.empty() ? such::ui::clear_font_preference(&error)
                                           : such::ui::save_font_preference(familyUtf8, &error);
        if (!ok && showError) {
            MessageBoxW(hwnd, widen_utf8("Could not save font preference: " + error).c_str(),
                        L"Such font settings", MB_OK | MB_ICONWARNING);
        }
    }
    reset_font_handles(hwnd);
    return true;
}

such::ui::FontCommand current_font_command() {
    return such::ui::parse_font_command(narrow_utf8(gQuery), such::ui::PlatformDialect::Windows);
}

bool font_picker_visible() {
    const auto cmd = current_font_command();
    return cmd.matched && cmd.show_picker;
}

void draw_font_picker(HDC dc, HWND hwnd, const such::ui::WindowLayout& layout, float scale) {
    if (!font_picker_visible()) return;
    const auto& options = such::ui::builtin_font_options();
    const int count = std::min(5, static_cast<int>(options.size()));
    for (int i = 0; i < count; ++i) {
        RECT row = to_rect(layout.rows[static_cast<std::size_t>(i)], scale);
        const bool selected = i == gFontPickerSelection;
        fill_round(dc, row, px(layout.metrics.corner_radius, scale), selected ? RGB(245, 243, 235) : kBright);
        stroke_round(dc, row, px(layout.metrics.corner_radius, scale), selected ? kDarkGreen : kHairline,
                     selected ? std::max(1, px(1.0f, scale)) : 1);
        RECT name = row;
        name.left += px(16.0f, scale);
        name.right -= px(120.0f, scale);
        SetTextColor(dc, kDarkGreen);
        HGDIOBJ oldFont = SelectObject(dc, gFilenameFont);
        const std::wstring label = widen_utf8(options[static_cast<std::size_t>(i)].label);
        DrawTextW(dc, label.c_str(), -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, gPathFont);
        RECT status = row;
        status.left = status.right - px(110.0f, scale);
        status.right -= px(14.0f, scale);
        const bool installed = font_family_available(hwnd, widen_utf8(options[static_cast<std::size_t>(i)].family));
        SetTextColor(dc, installed ? kPathGreen : RGB(150, 145, 136));
        DrawTextW(dc, installed ? L"Installed" : L"Not installed", -1, &status,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
    }
}

int hit_font_row(HWND hwnd, int xPx, int yPx) {
    if (!font_picker_visible()) return -1;
    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale, static_cast<float>(client.bottom) / scale);
    const int count = std::min(5, static_cast<int>(such::ui::builtin_font_options().size()));
    for (int i = 0; i < count; ++i) {
        RECT row = to_rect(layout.rows[static_cast<std::size_t>(i)], scale);
        if (PtInRect(&row, POINT{xPx, yPx})) return i;
    }
    return -1;
}

bool apply_font_picker_selection(HWND hwnd) {
    const auto& options = such::ui::builtin_font_options();
    if (gFontPickerSelection < 0 || gFontPickerSelection >= std::min(5, static_cast<int>(options.size()))) return false;
    const auto& option = options[static_cast<std::size_t>(gFontPickerSelection)];
    if (!apply_font_family(hwnd, option.family, true, true)) return false;
    SetWindowTextW(gSearch, L"");
    gQuery.clear();
    gSuggestions.clear();
    gResults.clear();
    return true;
}

bool execute_font_command(HWND hwnd) {
    const auto cmd = current_font_command();
    if (!cmd.matched) return false;
    if (cmd.show_picker) return apply_font_picker_selection(hwnd);
    if (cmd.requested_family.has_value()) {
        const std::string& requested = *cmd.requested_family;
        if (!apply_font_family(hwnd, requested, true, true)) return true;
        SetWindowTextW(gSearch, L"");
        gQuery.clear();
        gSuggestions.clear();
        gResults.clear();
        return true;
    }
    return true;
}

void update_scrollbar(HWND hwnd);
void refresh_query(HWND hwnd, bool resetScroll);

std::optional<std::filesystem::path> choose_index_root(HWND hwnd) {
    BROWSEINFOW browse{};
    browse.hwndOwner = hwnd;
    browse.lpszTitle = L"Choose a folder for Such to index";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse);
    if (!pidl) return std::nullopt;
    std::wstring buffer(32768, L'\0');
    const BOOL ok = SHGetPathFromIDListEx(pidl, buffer.data(), static_cast<DWORD>(buffer.size()), GPFIDL_DEFAULT);
    CoTaskMemFree(pidl);
    if (!ok) return std::nullopt;
    buffer.resize(std::wcslen(buffer.c_str()));
    return std::filesystem::path(buffer);
}

bool execute_index_command(HWND hwnd) {
    const auto command = such::ui::parse_index_command(narrow_utf8(gQuery), such::ui::PlatformDialect::Windows);
    if (!command.matched || !gRuntime) return false;
    std::string error;
    switch (command.kind) {
        case such::ui::IndexCommandKind::AddRoot: {
            std::optional<std::filesystem::path> root;
            if (command.argument.has_value()) root = std::filesystem::path(widen_utf8(*command.argument));
            else root = choose_index_root(hwnd);
            if (root && !gRuntime->add_root(*root, &error)) {
                MessageBoxW(hwnd, widen_utf8("Could not index folder: " + error).c_str(), L"Such", MB_OK | MB_ICONWARNING);
            }
            break;
        }
        case such::ui::IndexCommandKind::ReplaceRoot: {
            std::optional<std::filesystem::path> root;
            if (command.argument.has_value()) root = std::filesystem::path(widen_utf8(*command.argument));
            else root = choose_index_root(hwnd);
            if (root && !gRuntime->replace_roots({*root}, &error)) {
                MessageBoxW(hwnd, widen_utf8("Could not change search folder: " + error).c_str(), L"Such", MB_OK | MB_ICONWARNING);
            }
            break;
        }
        case such::ui::IndexCommandKind::Reindex:
            gRuntime->reindex_async();
            break;
        case such::ui::IndexCommandKind::ShowRoots:
            // Keep /roots visible so the list remains inspectable.
            refresh_query(hwnd, true);
            return true;
        case such::ui::IndexCommandKind::NoCommand:
            return false;
    }
    gRuntimeIndexing = gRuntime->status().indexing;
    SetWindowTextW(gSearch, L"");
    gQuery.clear();
    gSuggestions.clear();
    gResults.clear();
    gScrollDip = 0.0f;
    update_scrollbar(hwnd);
    if (gSearch) InvalidateRect(gSearch, nullptr, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool execute_agent_command(HWND hwnd) {
    const auto command = such::ui::parse_agent_command(narrow_utf8(gQuery), such::ui::PlatformDialect::Windows);
    if (!command.matched) return false;

    std::string error;
    std::vector<std::string> roots;
    if (gRuntime) {
        roots = gRuntime->roots(&error);
        if (!error.empty()) {
            MessageBoxW(hwnd, widen_utf8(error).c_str(), L"Such - Search roots unavailable", MB_OK | MB_ICONWARNING);
            return true;
        }
    }
    const auto cwd = such::ui::preferred_agent_working_directory(roots);
    if (!such::ui::launch_agent_terminal(command.kind, cwd, &error)) {
        MessageBoxW(hwnd, widen_utf8(error.empty() ? "Could not open the requested AI agent." : error).c_str(),
                    L"Such", MB_OK | MB_ICONWARNING);
    }
    SetWindowTextW(gSearch, L"");
    gQuery.clear();
    gSuggestions.clear();
    gResults.clear();
    gScrollDip = 0.0f;
    update_scrollbar(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool runtime_has_roots() {
    if (!gRuntime) return false;
    std::string error;
    const auto roots = gRuntime->roots(&error);
    if (!error.empty()) {
        gSearchError = "Could not list search roots: " + error;
        return false;
    }
    return !roots.empty();
}

bool choose_and_add_index_root(HWND hwnd, bool preserveQuery) {
    if (!gRuntime) return false;
    const auto root = choose_index_root(hwnd);
    if (!root.has_value()) return false;

    std::string error;
    if (!gRuntime->add_root(*root, &error)) {
        MessageBoxW(hwnd, widen_utf8("Could not index folder: " + error).c_str(),
                    L"Such", MB_OK | MB_ICONWARNING);
        return false;
    }

    // add_root() starts the worker asynchronously. Keep an ordinary search
    // query intact so the timer can populate matching rows as soon as the
    // catalog generation advances. /index itself remains a consumed command.
    if (!preserveQuery) {
        SetWindowTextW(gSearch, L"");
        gQuery.clear();
    }
    gSuggestions.clear();
    gResults.clear();
    gScrollDip = 0.0f;
    gRuntimeGeneration = gRuntime->generation();
    gRuntimeIndexing = gRuntime->status().indexing;
    update_scrollbar(hwnd);
    if (gSearch) InvalidateRect(gSearch, nullptr, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void update_scrollbar(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale,
        static_cast<float>(client.bottom) / scale);
    const float maxScroll = such::ui::max_result_scroll(layout, gResults.size());
    gScrollDip = std::clamp(gScrollDip, 0.0f, maxScroll);

    const int contentPx = std::max(0, px(such::ui::result_content_height(layout, gResults.size()), scale));
    const int pagePx = std::max(1, px(layout.results_viewport.height, scale));
    SCROLLINFO si{};
    si.cbSize = static_cast<UINT>(sizeof(si));
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = std::max(0, contentPx - 1);
    si.nPage = static_cast<UINT>(pagePx);
    si.nPos = px(gScrollDip, scale);
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

void set_scroll_dip(HWND hwnd, float value) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale,
        static_cast<float>(client.bottom) / scale);
    gScrollDip = std::clamp(value, 0.0f, such::ui::max_result_scroll(layout, gResults.size()));
    gSwipe.close();
    update_scrollbar(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}


std::vector<such::ui::ResultItem> root_items() {
    std::vector<such::ui::ResultItem> items;
    if (!gRuntime) return items;
    std::string error;
    const auto roots = gRuntime->roots(&error);
    if (!error.empty()) {
        gSearchError = "Could not list search roots: " + error;
        return items;
    }
    for (const auto& root : roots) {
        such::ui::ResultItem item;
        item.file_id = static_cast<std::uint64_t>(std::hash<std::string>{}(root));
        item.path = root;
        const auto rootPath = std::filesystem::path(widen_utf8(root));
        item.filename = rootPath.filename().empty() ? root : narrow_utf8(rootPath.filename().wstring());
        item.indexed = true;
        items.push_back(std::move(item));
    }
    return items;
}

void refresh_query(HWND hwnd, bool resetScroll = true) {
    const int len = GetWindowTextLengthW(gSearch);
    std::wstring text(static_cast<std::size_t>(std::max(0, len)) + 1, L'\0');
    if (len > 0) GetWindowTextW(gSearch, text.data(), len + 1);
    text.resize(static_cast<std::size_t>(std::max(0, len)));
    gQuery = text;

    const auto q = narrow_utf8(gQuery);
    gSearchError.clear();
    const auto agentCommand = such::ui::parse_agent_command(q, such::ui::PlatformDialect::Windows);
    const auto fontCommand = such::ui::parse_font_command(q, such::ui::PlatformDialect::Windows);
    const auto indexCommand = such::ui::parse_index_command(q, such::ui::PlatformDialect::Windows);
    const auto observed = gRuntime ? gRuntime->observed_extensions()
                                   : std::vector<std::string>{"pdf", "dwg", "xlsx", "docx", "3dm", "cpp", "rs", "ifc", "rvt"};
    if (agentCommand.matched) {
        gSuggestions.clear();
        gResults.clear();
    } else if (fontCommand.matched) {
        gSuggestions.clear();
        gResults.clear();
        gFontPickerSelection = std::clamp(gFontPickerSelection, 0, 4);
    } else if (indexCommand.matched) {
        gSuggestions = such::ui::autocomplete(q, such::ui::PlatformDialect::Windows, observed);
        if (indexCommand.kind == such::ui::IndexCommandKind::ShowRoots) gResults = root_items();
        else gResults.clear();
    } else {
        gSuggestions = such::ui::autocomplete(q, such::ui::PlatformDialect::Windows, observed);
        if (gDemo) {
            const auto now = static_cast<std::int64_t>(std::time(nullptr));
            gResults = such::ui::make_demo_results(q, such::ui::PlatformDialect::Windows, now);
        } else if (gRuntime) {
            gResults = gRuntime->search(q, such::ui::PlatformDialect::Windows, 0, &gSearchError);
        } else {
            gResults.clear();
        }
    }
    gSelectedRow = gResults.empty() ? -1 : std::clamp(gSelectedRow, 0, static_cast<int>(gResults.size()) - 1);
    if (resetScroll) gScrollDip = 0.0f;
    gSwipe.close();
    update_scrollbar(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

int hit_row(HWND hwnd, int xPx, int yPx) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale,
        static_cast<float>(client.bottom) / scale);
    const float x = static_cast<float>(xPx) / scale;
    const float y = static_cast<float>(yPx) / scale;
    const auto& viewport = layout.results_viewport;
    if (x < viewport.x || x >= viewport.x + viewport.width || y < viewport.y || y >= viewport.y + viewport.height) return -1;
    const float stride = layout.metrics.row_height + layout.metrics.row_gap;
    if (stride <= 0.0f) return -1;
    const float contentY = y - viewport.y + gScrollDip;
    const int index = static_cast<int>(std::floor(contentY / stride));
    if (index < 0 || index >= static_cast<int>(gResults.size())) return -1;
    const auto row = such::ui::result_row_rect(layout, static_cast<std::size_t>(index), gScrollDip);
    if (y < row.y || y >= row.y + row.height) return -1; // gap
    return index;
}

void open_location(const such::ui::ResultItem& item) {
    const std::wstring path = widen_utf8(item.path);
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void open_properties(const such::ui::ResultItem& item) {
    const std::wstring path = widen_utf8(item.path);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = static_cast<DWORD>(sizeof(sei));
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.lpVerb = L"properties";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOW;
    ShellExecuteExW(&sei);
}

void open_item(HWND hwnd, int rowIndex) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gResults.size())) return;
    const auto path = widen_utf8(gResults[static_cast<std::size_t>(rowIndex)].path);
    ShellExecuteW(hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void show_appearance_menu(HWND hwnd, int rowIndex, POINT screenPoint) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gResults.size())) return;
    HMENU menu = CreatePopupMenu();
    HMENU colors = CreatePopupMenu();
    AppendMenuW(colors, MF_STRING, kMenuAccentNone, L"No color");
    AppendMenuW(colors, MF_STRING, kMenuAccentGreen, L"Dark green");
    AppendMenuW(colors, MF_STRING, kMenuAccentSand, L"Sand");
    AppendMenuW(colors, MF_STRING, kMenuAccentBlue, L"Blue");
    AppendMenuW(colors, MF_STRING, kMenuAccentRose, L"Rose");
    HMENU icons = CreatePopupMenu();
    AppendMenuW(icons, MF_STRING, kMenuIconSystem, L"System default");
    AppendMenuW(icons, MF_STRING, kMenuIconDocument, L"System document");
    AppendMenuW(icons, MF_STRING, kMenuIconFolder, L"System folder");
    AppendMenuW(icons, MF_STRING, kMenuIconFavorite, L"System favorite");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(icons), L"Icon");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(colors), L"Color");
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hwnd, nullptr);
    auto& item = gResults[static_cast<std::size_t>(rowIndex)];
    switch (cmd) {
        case kMenuAccentNone: item.accent = such::ui::AccentTone::None; break;
        case kMenuAccentGreen: item.accent = such::ui::AccentTone::Green; break;
        case kMenuAccentSand: item.accent = such::ui::AccentTone::Sand; break;
        case kMenuAccentBlue: item.accent = such::ui::AccentTone::Blue; break;
        case kMenuAccentRose: item.accent = such::ui::AccentTone::Rose; break;
        case kMenuIconSystem: item.icon_override = such::ui::IconOverride::SystemDefault; break;
        case kMenuIconDocument: item.icon_override = such::ui::IconOverride::Document; break;
        case kMenuIconFolder: item.icon_override = such::ui::IconOverride::Folder; break;
        case kMenuIconFavorite: item.icon_override = such::ui::IconOverride::Favorite; break;
        default: break;
    }
    DestroyMenu(menu);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void perform_action(HWND hwnd, int rowIndex, ResultAction action, POINT screenPoint) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gResults.size())) return;
    auto& item = gResults[static_cast<std::size_t>(rowIndex)];
    switch (action) {
        case ResultAction::OpenLocation:
            open_location(item);
            break;
        case ResultAction::ToggleIndex:
            if (gDemo) item.indexed = !item.indexed;
            else if (gRuntime) {
                std::string error;
                if (!gRuntime->set_indexed(item.path, !item.indexed, &error)) {
                    MessageBoxW(hwnd, widen_utf8("Could not change index state: " + error).c_str(), L"Such", MB_OK | MB_ICONWARNING);
                }
                refresh_query(hwnd, false);
            }
            break;
        case ResultAction::TogglePin:
            if (gDemo) item.pinned = !item.pinned;
            else if (gRuntime) {
                std::string error;
                if (!gRuntime->set_pinned(item.path, !item.pinned, &error)) {
                    MessageBoxW(hwnd, widen_utf8("Could not change pin state: " + error).c_str(), L"Such", MB_OK | MB_ICONWARNING);
                }
                refresh_query(hwnd, false);
            }
            break;
        case ResultAction::OpenProperties:
            open_properties(item);
            break;
        case ResultAction::EditAppearance:
            if (gDemo) show_appearance_menu(hwnd, rowIndex, screenPoint);
            break;
    }
    gSwipe.close();
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool hit_swipe_action(HWND hwnd, int xPx, int yPx, int& rowIndex, ResultAction& action) {
    if (!gSwipe.is_open()) return false;
    rowIndex = gSwipe.state().row;
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gResults.size())) return false;

    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale,
        static_cast<float>(client.bottom) / scale);
    RECT row = to_rect(such::ui::result_row_rect(layout, static_cast<std::size_t>(rowIndex), gScrollDip), scale);
    if (yPx < row.top || yPx >= row.bottom) return false;

    const int actionWidth = px(layout.metrics.swipe_action_width, scale);
    const int total = actionWidth * 3;
    const int offset = px(gSwipe.state().offset_dip, scale);
    int index = -1;
    if (gSwipe.state().side == SwipeSide::Trailing) {
        const int rowRight = static_cast<int>(row.right);
        const int start = rowRight - total;
        const int revealed_start = std::max(start, rowRight + offset);
        if (xPx >= revealed_start && xPx < rowRight) index = (xPx - start) / actionWidth;
    } else {
        const int rowLeft = static_cast<int>(row.left);
        const int end = rowLeft + total;
        const int revealed_end = std::min(end, rowLeft + offset);
        if (xPx >= rowLeft && xPx < revealed_end) index = (xPx - rowLeft) / actionWidth;
    }
    if (index < 0 || index > 2) return false;
    action = such::ui::actions_for_side(gSwipe.state().side)[static_cast<std::size_t>(index)];
    return true;
}

void show_context_menu(HWND hwnd, int rowIndex, POINT screenPoint) {
    if (rowIndex < 0 || rowIndex >= static_cast<int>(gResults.size())) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuLocation, L"Open location");
    AppendMenuW(menu, MF_STRING, kMenuIndex, gResults[static_cast<std::size_t>(rowIndex)].indexed ? L"Remove from index" : L"Index");
    AppendMenuW(menu, MF_STRING, kMenuPin, gResults[static_cast<std::size_t>(rowIndex)].pinned ? L"Unpin" : L"Pin");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuProperties, L"Properties");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuAccentGreen, L"Appearance…");
    const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
        case kMenuLocation: perform_action(hwnd, rowIndex, ResultAction::OpenLocation, screenPoint); break;
        case kMenuIndex: perform_action(hwnd, rowIndex, ResultAction::ToggleIndex, screenPoint); break;
        case kMenuPin: perform_action(hwnd, rowIndex, ResultAction::TogglePin, screenPoint); break;
        case kMenuProperties: perform_action(hwnd, rowIndex, ResultAction::OpenProperties, screenPoint); break;
        case kMenuAccentGreen: perform_action(hwnd, rowIndex, ResultAction::EditAppearance, screenPoint); break;
        default: break;
    }
}

void draw_autocomplete(HDC dc, const RECT& search, float scale, const such::ui::ResponsiveMetrics& m) {
    if (gSuggestions.empty()) return;
    const int itemH = px(30.0f, scale);
    const int count = std::min(6, static_cast<int>(gSuggestions.size()));
    RECT popup{search.left, search.bottom + px(4.0f, scale), search.right,
               search.bottom + px(4.0f, scale) + itemH * count};
    fill_round(dc, popup, px(m.corner_radius, scale), kBright);
    stroke_round(dc, popup, px(m.corner_radius, scale), kHairline);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(dc, gPathFont);
    for (int i = 0; i < count; ++i) {
        RECT r{popup.left + px(12.0f, scale), popup.top + i * itemH,
               popup.right - px(12.0f, scale), popup.top + (i + 1) * itemH};
        SetTextColor(dc, kDarkGreen);
        const std::wstring token = widen_utf8(gSuggestions[static_cast<std::size_t>(i)].token);
        DrawTextW(dc, token.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT label = r;
        label.left += px(110.0f, scale);
        SetTextColor(dc, kPathGreen);
        const std::wstring desc = widen_utf8(gSuggestions[static_cast<std::size_t>(i)].label);
        DrawTextW(dc, desc.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
}

void draw_detail_tree(HDC dc, const such::ui::WindowLayout& layout, float scale) {
    const auto detail = such::ui::parse_detail_search(narrow_utf8(gQuery));
    if (!detail.active || detail.refinements.empty()) return;
    const auto& m = layout.metrics;
    const float branch_h = std::clamp(m.row_height * 0.50f, 28.0f, 36.0f);
    const float branch_gap = std::clamp(m.row_gap * 0.75f, 4.0f, 7.0f);
    const float base_y = layout.security.y + layout.security.height + m.security_to_results_gap;
    HGDIOBJ oldFont = SelectObject(dc, gPathFont);
    SetBkMode(dc, TRANSPARENT);
    for (std::size_t i = 0; i < detail.refinements.size(); ++i) {
        const float indent = 18.0f * static_cast<float>(i + 1);
        const float y = base_y + static_cast<float>(i) * (branch_h + branch_gap);
        const float width = std::max(150.0f, layout.search.width * 0.72f - indent);
        such::ui::RectF rf{layout.search.x + indent, y, width, branch_h};
        RECT r = to_rect(rf, scale);
        // Vector branch connector + rounded child search field.
        HPEN pen = CreatePen(PS_SOLID, std::max(1, px(1.0f, scale)), kHairline);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        const int branchX = px(layout.search.x + indent - 9.0f, scale);
        const int midY = (r.top + r.bottom) / 2;
        MoveToEx(dc, branchX, px(y - branch_gap, scale), nullptr);
        LineTo(dc, branchX, midY);
        LineTo(dc, r.left, midY);
        SelectObject(dc, oldPen); DeleteObject(pen);
        fill_round(dc, r, px(6.0f, scale), kBright);
        stroke_round(dc, r, px(6.0f, scale), kHairline, std::max(1, px(1.0f, scale)));
        RECT text = r; text.left += px(11.0f, scale); text.right -= px(8.0f, scale);
        const std::wstring label = detail.refinements[i].empty() ? L"Detail search…" : widen_utf8(detail.refinements[i]);
        SetTextColor(dc, detail.refinements[i].empty() ? kPlaceholderGray : kDarkGreen);
        DrawTextW(dc, label.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, oldFont);
}


void draw_security_toggle(HDC dc, const such::ui::WindowLayout& layout, float scale) {
    const RECT row = to_rect(layout.security, scale);
    HGDIOBJ oldFont = SelectObject(dc, gPathFont ? gPathFont : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kDarkGreen);

    RECT label = row;
    label.left += px(2.0f, scale);
    label.right = label.left + px(64.0f, scale);
    DrawTextW(dc, L"Security", -1, &label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    const int switchW = px(34.0f, scale);
    const int switchH = px(18.0f, scale);
    const int switchX = row.left + px(72.0f, scale);
    const int switchY = (row.top + row.bottom - switchH) / 2;
    RECT sw{switchX, switchY, switchX + switchW, switchY + switchH};
    fill_round(dc, sw, switchH / 2, gSecurityGate.active() ? kDarkGreen : kHairline);

    const int knob = std::max(px(10.0f, scale), switchH - px(4.0f, scale));
    const int knobX = gSecurityGate.active()
        ? sw.right - knob - px(2.0f, scale)
        : sw.left + px(2.0f, scale);
    const int knobY = sw.top + (switchH - knob) / 2;
    RECT k{knobX, knobY, knobX + knob, knobY + knob};
    fill_round(dc, k, knob / 2, kBright);

    SelectObject(dc, oldFont);
}

bool hit_security_toggle(HWND hwnd, int x, int y) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale,
        static_cast<float>(client.bottom) / scale);
    const RECT row = to_rect(layout.security, scale);
    return point_in_rect(row, x, y);
}

void show_enterprise_security_notice(HWND hwnd) {
    const wchar_t* message =
        L"보안검색은 본사와 연락이 필요합니다.\n\n"
        L"Enterprise Such needs connection to corporation\n\n"
        L"https://such.heritage-labs.net\n\n"
        L"Open the Enterprise Such website?";
    const int answer = MessageBoxW(
        hwnd, message, L"Such Enterprise Security",
        MB_OKCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON1);
    if (answer == IDOK) {
        (void)ShellExecuteW(
            hwnd, L"open", L"https://such.heritage-labs.net",
            nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void request_security_toggle(HWND hwnd) {
    if (gSecurityGate.active()) {
        (void)gSecurityGate.request_disable();
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    std::string error;
    const auto decision = gSecurityGate.request_enable(error);
    switch (decision) {
        case such::security::SecureSearchDecision::Activated:
            break;
        case such::security::SecureSearchDecision::RequiresEnterpriseActivation:
            show_enterprise_security_notice(hwnd);
            break;
        case such::security::SecureSearchDecision::Denied:
            MessageBoxW(
                hwnd,
                widen_utf8(error.empty() ? "Secure Search activation was denied" : error).c_str(),
                L"Such Enterprise Security",
                MB_OK | MB_ICONWARNING);
            break;
        case such::security::SecureSearchDecision::Deactivated:
            break;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC windowDc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    const int height = std::max(1, static_cast<int>(client.bottom - client.top));

    HDC dc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, width, height);
    const HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(width) / scale,
        static_cast<float>(height) / scale);
    const auto& m = layout.metrics;
    ensure_fonts(hwnd, m);
    gSwipe.set_action_tray_width(m.swipe_action_width * 3.0f);

    HBRUSH bg = CreateSolidBrush(kPaper);
    FillRect(dc, &client, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);

    RECT search = to_rect(layout.search, scale);
    fill_round(dc, search, px(m.search_radius, scale), kBright);
    stroke_round(dc, search, px(m.search_radius, scale), kDarkGreen,
                 std::max(1, px(1.0f, scale)));
    RECT inner = search;
    InflateRect(&inner, -1, -1);
    HPEN hi = CreatePen(PS_SOLID, 1, kHighlight);
    HGDIOBJ oldPen = SelectObject(dc, hi);
    MoveToEx(dc, inner.left + px(m.search_radius, scale), inner.top, nullptr);
    LineTo(dc, inner.right - px(m.search_radius, scale), inner.top);
    SelectObject(dc, oldPen);
    DeleteObject(hi);
    draw_search_glyph(dc,
                      static_cast<int>(search.left) + px(20.0f, scale),
                      static_cast<int>((search.top + search.bottom) / 2),
                      px(14.0f, scale), kDarkGreen);
    if (!gQuery.empty()) {
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(hwnd, &cursor);
        const RECT clear = search_clear_rect(search, scale);
        draw_clear_button(dc, clear, point_in_rect(clear, cursor.x, cursor.y));
    }

    draw_security_toggle(dc, layout, scale);
    draw_detail_tree(dc, layout, scale);

    const RECT viewportPx = to_rect(layout.results_viewport, scale);
    const int savedDc = SaveDC(dc);
    IntersectClipRect(dc, viewportPx.left, viewportPx.top, viewportPx.right, viewportPx.bottom);
    const float stride = m.row_height + m.row_gap;
    const int firstRow = stride > 0.0f ? std::max(0, static_cast<int>(std::floor(gScrollDip / stride))) : 0;
    const int drawCount = std::max(2, m.visible_results + 2);
    const int lastRow = std::min(static_cast<int>(gResults.size()), firstRow + drawCount);
    for (int i = firstRow; i < lastRow; ++i) {
        const int offset = gSwipe.state().row == i ? px(gSwipe.state().offset_dip, scale) : 0;
        draw_result_row(dc, gResults[static_cast<std::size_t>(i)],
                        to_rect(such::ui::result_row_rect(layout, static_cast<std::size_t>(i), gScrollDip), scale), i, offset,
                        scale, m, gSelectedRow == i, gHoverRow == i);
    }
    RestoreDC(dc, savedDc);

    draw_font_picker(dc, hwnd, layout, scale);

    if (!font_picker_visible() && !gQuery.empty() && gResults.empty()) {
        std::wstring emptyText;
        if (!gDemo && gRuntime) {
            const auto status = gRuntime->status();
            if (!gSearchError.empty()) emptyText = widen_utf8("Search error: " + gSearchError);
            else if (status.roots.empty()) emptyText = L"Press Enter to choose a folder to search";
            else if (status.indexing) emptyText = L"Indexing files…";
            else emptyText = L"No matches";
        } else if (gDemo || !gSuggestions.empty()) {
            emptyText = L"No matches";
        }
        if (!emptyText.empty()) {
            RECT noMatches = to_rect(such::ui::result_row_rect(layout, 0, 0.0f), scale);
            SelectObject(dc, gPathFont);
            SetTextColor(dc, kPathGreen);
            DrawTextW(dc, emptyText.c_str(), -1, &noMatches, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    // Popup is transient and overlays content instead of becoming permanent chrome.
    draw_autocomplete(dc, search, scale, m);

    BitBlt(windowDc, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

void layout_children(HWND hwnd) {
    if (!gSearch) return;
    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale,
        static_cast<float>(client.bottom) / scale);
    ensure_fonts(hwnd, layout.metrics);
    RECT s = to_rect(layout.search, scale);
    const int leftInset = px(kSearchLeftContentInsetDip, scale);
    const int rightInset = px(kSearchRightContentInsetDip, scale);
    const int verticalInset = px(5.0f, scale);
    const int searchX = static_cast<int>(s.left) + leftInset;
    const int searchY = static_cast<int>(s.top) + verticalInset;
    const int searchW = std::max(
        1, static_cast<int>(s.right - s.left) - leftInset - rightInset);
    const int searchH = std::max(
        1, static_cast<int>(s.bottom - s.top) - verticalInset * 2);
    MoveWindow(gSearch, searchX, searchY, searchW, searchH, TRUE);
    SendMessageW(gSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(1.0f, scale), px(1.0f, scale)));
    SetWindowRgn(gSearch, nullptr, TRUE);
    update_scrollbar(hwnd);
}

void autocomplete_first(HWND hwnd) {
    if (gSuggestions.empty()) return;
    std::string current = narrow_utf8(gQuery);
    const auto pos = current.find_last_of(" \t\n");
    current = (pos == std::string::npos ? std::string{} : current.substr(0, pos + 1)) + gSuggestions.front().token + " ";
    const std::wstring completed = widen_utf8(current);
    SetWindowTextW(gSearch, completed.c_str());
    SendMessageW(gSearch, EM_SETSEL, static_cast<WPARAM>(completed.size()), static_cast<LPARAM>(completed.size()));
    refresh_query(hwnd);
}

void move_selection(HWND hwnd, int delta) {
    if (gResults.empty()) return;
    if (gSelectedRow < 0) gSelectedRow = delta > 0 ? 0 : static_cast<int>(gResults.size()) - 1;
    else gSelectedRow = std::clamp(gSelectedRow + delta, 0, static_cast<int>(gResults.size()) - 1);

    RECT client{};
    GetClientRect(hwnd, &client);
    const float scale = scale_for_window(hwnd);
    const auto layout = window_layout_for_query(
        static_cast<float>(client.right) / scale, static_cast<float>(client.bottom) / scale);
    const auto row = such::ui::result_row_rect(layout, static_cast<std::size_t>(gSelectedRow), gScrollDip);
    if (row.y < layout.results_viewport.y) {
        set_scroll_dip(hwnd, static_cast<float>(gSelectedRow) * (layout.metrics.row_height + layout.metrics.row_gap));
    } else if (row.y + row.height > layout.results_viewport.y + layout.results_viewport.height) {
        set_scroll_dip(hwnd, gScrollDip + (row.y + row.height - (layout.results_viewport.y + layout.results_viewport.height)));
    }
    gSwipe.close();
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK search_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND parent = GetParent(hwnd);
    if (msg == WM_MOUSEWHEEL) {
        return SendMessageW(parent, WM_MOUSEWHEEL, wp, lp);
    }
    if (msg == WM_KEYDOWN) {
        switch (wp) {
            case VK_TAB:
                SendMessageW(parent, kMsgAutocomplete, 0, 0);
                return 0;
            case VK_ESCAPE:
                SendMessageW(parent, kMsgCloseTransient, 0, 0);
                return 0;
            case VK_DOWN:
                if (font_picker_visible()) { gFontPickerSelection = std::min(4, gFontPickerSelection + 1); InvalidateRect(parent, nullptr, FALSE); }
                else move_selection(parent, +1);
                return 0;
            case VK_UP:
                if (font_picker_visible()) { gFontPickerSelection = std::max(0, gFontPickerSelection - 1); InvalidateRect(parent, nullptr, FALSE); }
                else move_selection(parent, -1);
                return 0;
            case VK_RETURN:
                if (execute_agent_command(parent) || execute_font_command(parent) || execute_index_command(parent)) return 0;
                if (gRuntime && !runtime_has_roots()) {
                    SendMessageW(parent, kMsgEnsureIndexRoot, gQuery.empty() ? 0 : 1, 0);
                    return 0;
                }
                SendMessageW(parent, kMsgOpenSelected, 0, 0);
                return 0;
            default: break;
        }
    }
    return CallWindowProcW(gOldSearchProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            gSearch = CreateWindowExW(0, kSearchClass, L"",
                                      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)),
                                      reinterpret_cast<LPCREATESTRUCTW>(lp)->hInstance, nullptr);
            if (!gSearch) return -1;
            const std::wstring searchPlaceholder = widen_utf8(such::ui::design::kSearchPlaceholder);
            SendMessageW(gSearch, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(searchPlaceholder.c_str()));
            gOldSearchProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gSearch, GWLP_WNDPROC,
                                                                         reinterpret_cast<LONG_PTR>(search_proc)));
            layout_children(hwnd);
            SetFocus(gSearch);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            const float s = scale_for_window(hwnd);
            info->ptMinTrackSize.x = static_cast<LONG>(420.0f * s);
            info->ptMinTrackSize.y = static_cast<LONG>(390.0f * s);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SETFOCUS:
            if (gSearch) SetFocus(gSearch);
            return 0;
        case WM_SIZE:
            layout_children(hwnd);
            gSwipe.close();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            if (const auto* suggested = reinterpret_cast<const RECT*>(lp)) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            gFontDpi = 0;
            gFontHeights = {};
            clear_icon_cache();
            layout_children(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, kDarkGreen);
            SetBkColor(dc, kBright);
            if (!gSearchBrush) gSearchBrush = CreateSolidBrush(kBright);
            return reinterpret_cast<LRESULT>(gSearchBrush);
        }
        case WM_COMMAND:
            if (LOWORD(wp) == kSearchId && HIWORD(wp) == EN_CHANGE) refresh_query(hwnd);
            return 0;
        case kMsgAutocomplete:
            autocomplete_first(hwnd);
            return 0;
        case kMsgCloseTransient:
            gSuggestions.clear();
            gSwipe.close();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case kMsgOpenSelected:
            open_item(hwnd, gSelectedRow);
            return 0;
        case kMsgEnsureIndexRoot:
            (void)choose_and_add_index_root(hwnd, wp != 0);
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            RECT client{};
            GetClientRect(hwnd, &client);
            const float scale = scale_for_window(hwnd);
            const auto layout = window_layout_for_query(
                static_cast<float>(client.right) / scale, static_cast<float>(client.bottom) / scale);
            const float step = (layout.metrics.row_height + layout.metrics.row_gap) * 2.5f;
            set_scroll_dip(hwnd, gScrollDip - static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA) * step);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = static_cast<UINT>(sizeof(si));
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= 36; break;
                case SB_LINEDOWN: pos += 36; break;
                case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK: pos = si.nTrackPos; break;
                case SB_TOP: pos = si.nMin; break;
                case SB_BOTTOM: pos = si.nMax; break;
                default: return 0;
            }
            const float scale = scale_for_window(hwnd);
            set_scroll_dip(hwnd, static_cast<float>(std::max(0, pos)) / scale);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (GetTickCount64() < gIgnoreMouseUntil) return 0;
            const int x = GET_X_LPARAM(lp);
            const int y = GET_Y_LPARAM(lp);
            if (gSwipe.state().dragging) {
                const ULONGLONG now = GetTickCount64();
                const float dt = gLastPointerTick == 0 ? 0.016f : std::max(0.001f, static_cast<float>(now - gLastPointerTick) / 1000.0f);
                gLastPointerTick = now;
                const float scale = scale_for_window(hwnd);
                gSwipe.update(static_cast<float>(x) / scale, static_cast<float>(y) / scale, dt);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            const int hover = hit_row(hwnd, x, y);
            if (hover != gHoverRow) {
                gHoverRow = hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{};
            tme.cbSize = static_cast<DWORD>(sizeof(tme));
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            return 0;
        }
        case WM_MOUSELEAVE:
            gHoverRow = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN: {
            if (GetTickCount64() < gIgnoreMouseUntil) return 0;
            const int x = GET_X_LPARAM(lp);
            const int y = GET_Y_LPARAM(lp);
            if (hit_security_toggle(hwnd, x, y)) {
                request_security_toggle(hwnd);
                return 0;
            }
            SetFocus(gSearch);
            RECT client{};
            GetClientRect(hwnd, &client);
            const float uiScale = scale_for_window(hwnd);
            const auto clickLayout = window_layout_for_query(
                static_cast<float>(client.right) / uiScale,
                static_cast<float>(client.bottom) / uiScale);
            const RECT search = to_rect(clickLayout.search, uiScale);
            const RECT clear = search_clear_rect(search, uiScale);
            if (!gQuery.empty() && point_in_rect(clear, x, y)) {
                SetWindowTextW(gSearch, L"");
                SendMessageW(gSearch, EM_SETSEL, 0, 0);
                refresh_query(hwnd);
                return 0;
            }
            if (const int fontRow = hit_font_row(hwnd, x, y); fontRow >= 0) {
                gFontPickerSelection = fontRow;
                (void)apply_font_picker_selection(hwnd);
                SetFocus(gSearch);
                return 0;
            }
            int actionRow = -1;
            ResultAction action{};
            if (hit_swipe_action(hwnd, x, y, actionRow, action)) {
                POINT screen{x, y}; ClientToScreen(hwnd, &screen);
                perform_action(hwnd, actionRow, action, screen);
                return 0;
            }
            const int row = hit_row(hwnd, x, y);
            if (row >= 0) {
                gSelectedRow = row;
                const float scale = scale_for_window(hwnd);
                gSwipe.begin(row, static_cast<float>(x) / scale, static_cast<float>(y) / scale);
                gLastPointerTick = GetTickCount64();
                SetCapture(hwnd);
            } else {
                gSwipe.close();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (GetTickCount64() < gIgnoreMouseUntil) return 0;
            if (gSwipe.state().dragging) {
                RECT client{};
                GetClientRect(hwnd, &client);
                const float scale = scale_for_window(hwnd);
                const int row = gSwipe.state().row;
                if (auto action = gSwipe.end(static_cast<float>(client.right) / scale)) {
                    POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                    ClientToScreen(hwnd, &screen);
                    perform_action(hwnd, row, *action, screen);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            // Release after end(); WM_CAPTURECHANGED may be delivered synchronously.
            // Releasing first used to cancel every mouse swipe before it could snap open.
            if (GetCapture() == hwnd) ReleaseCapture();
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            const int row = hit_row(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (row >= 0) {
                gSelectedRow = row;
                gSwipe.close();
                open_item(hwnd, row);
            }
            return 0;
        }
        case WM_POINTERDOWN: {
            POINT client{}; UINT32 pointerId = 0;
            if (!pointer_client_point(hwnd, wp, client, pointerId)) return 0;
            if (gActivePointerId != 0 && gActivePointerId != pointerId) return 0;
            gIgnoreMouseUntil = GetTickCount64() + 350;
            if (hit_security_toggle(hwnd, client.x, client.y)) {
                request_security_toggle(hwnd);
                return 0;
            }
            int actionRow = -1; ResultAction action{};
            if (hit_swipe_action(hwnd, client.x, client.y, actionRow, action)) {
                POINT screen = client; ClientToScreen(hwnd, &screen);
                perform_action(hwnd, actionRow, action, screen);
                return 0;
            }
            const int row = hit_row(hwnd, client.x, client.y);
            if (row >= 0) {
                gSelectedRow = row;
                gActivePointerId = pointerId;
                const float scale = scale_for_window(hwnd);
                gSwipe.begin(row, static_cast<float>(client.x) / scale, static_cast<float>(client.y) / scale);
                gLastPointerTick = GetTickCount64();
                // WM_POINTERDOWN contact is implicitly captured by Win32 until contact
                // ends or capture changes. Do not call WinRT/XAML-style pointer-capture
                // APIs from a classic HWND frontend.
            } else {
                gSwipe.close();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_POINTERUPDATE: {
            POINT client{}; UINT32 pointerId = 0;
            if (!pointer_client_point(hwnd, wp, client, pointerId) || pointerId != gActivePointerId || !gSwipe.state().dragging) return 0;
            const ULONGLONG now = GetTickCount64();
            const float dt = gLastPointerTick == 0 ? 0.016f : std::max(0.001f, static_cast<float>(now - gLastPointerTick) / 1000.0f);
            gLastPointerTick = now;
            const float scale = scale_for_window(hwnd);
            gSwipe.update(static_cast<float>(client.x) / scale, static_cast<float>(client.y) / scale, dt);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_POINTERUP: {
            POINT client{};
            UINT32 pointerId = 0;
            if (!pointer_client_point(hwnd, wp, client, pointerId) || pointerId != gActivePointerId) return 0;
            if (gSwipe.state().dragging) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                const float scale = scale_for_window(hwnd);
                const int row = gSwipe.state().row;
                if (auto action = gSwipe.end(static_cast<float>(rc.right) / scale)) {
                    POINT screen = client;
                    ClientToScreen(hwnd, &screen);
                    perform_action(hwnd, row, *action, screen);
                }
            }
            // Win32 implicitly owns pointer capture for contact input. WM_POINTERUP ends
            // that implicit capture; only clear our local pointer identity after end().
            gActivePointerId = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_POINTERCAPTURECHANGED: {
            const UINT32 lostPointerId = GET_POINTERID_WPARAM(wp);
            if (gActivePointerId != 0 && lostPointerId == gActivePointerId) {
                gActivePointerId = 0;
                if (gSwipe.state().dragging) gSwipe.close();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            if (gSwipe.state().dragging && gActivePointerId == 0) gSwipe.close();
            return 0;
        case WM_CONTEXTMENU: {
            POINT screen{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int row = -1;
            if (screen.x == -1 && screen.y == -1) {
                row = gSelectedRow;
                RECT client{}; GetClientRect(hwnd, &client);
                screen = POINT{client.left + static_cast<LONG>(40),
                               client.top + static_cast<LONG>(80)};
                ClientToScreen(hwnd, &screen);
            } else {
                POINT client = screen; ScreenToClient(hwnd, &client);
                row = hit_row(hwnd, client.x, client.y);
            }
            if (row >= 0) show_context_menu(hwnd, row, screen);
            return 0;
        }
        case WM_RBUTTONUP: {
            const int x = GET_X_LPARAM(lp);
            const int y = GET_Y_LPARAM(lp);
            const int row = hit_row(hwnd, x, y);
            if (row >= 0) {
                POINT screen{x, y}; ClientToScreen(hwnd, &screen);
                show_context_menu(hwnd, row, screen);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                gSwipe.close();
                gSuggestions.clear();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        case WM_PAINT:
            paint(hwnd);
            return 0;
        case WM_TIMER:
            if (wp == 1 || wp == 2) { DestroyWindow(hwnd); return 0; }
            if (wp == kTimerRuntimeRefresh && gRuntime) {
                const auto status = gRuntime->status();
                const auto generation = status.generation;
                const bool activityChanged = status.indexing != gRuntimeIndexing;
                if (generation != gRuntimeGeneration || status.indexed_files != gRuntimeIndexedFiles) {
                    gRuntimeGeneration = generation;
                    gRuntimeIndexedFiles = status.indexed_files;
                    refresh_query(hwnd, false);
                }
                if (activityChanged) {
                    gRuntimeIndexing = status.indexing;
                    if (gSearch) InvalidateRect(gSearch, nullptr, TRUE);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            return 0;
        case WM_DESTROY:
            delete_font(gSearchFont);
            delete_font(gFilenameFont);
            delete_font(gPathFont);
            delete_font(gBadgeFont);
            clear_icon_cache();
            if (gSearchBrush) {
                DeleteObject(gSearchBrush);
                gSearchBrush = nullptr;
            }
            KillTimer(hwnd, kTimerRuntimeRefresh);
            gRuntime.reset();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    [[maybe_unused]] ScopedComApartment comApartment;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (const auto preferred = such::ui::load_font_preference(); preferred && !preferred->empty()) {
        gFontFamily = widen_utf8(*preferred);
    }

    int leaseHoldMs = 0;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (std::wcscmp(argv[i], L"--smoke-hold") == 0) {
                gSmokeHold = true;
            } else if (std::wcscmp(argv[i], L"--smoke") == 0) {
                gSmoke = true;
            } else if (std::wcscmp(argv[i], L"--demo") == 0) {
                gDemo = true;
            } else if (std::wcscmp(argv[i], L"--lease-hold-ms") == 0 && i + 1 < argc) {
                wchar_t* end = nullptr;
                const long parsed = std::wcstol(argv[++i], &end, 10);
                if (end && *end == L'\0' && parsed > 0) {
                    leaseHoldMs = static_cast<int>(std::min<long>(parsed, 60'000L));
                }
            }
        }
        LocalFree(argv);
    }
    if (gSmokeHold) gSmoke = false;

    such::ui::FrontendLease lease;
    if (!gSmoke) {
        lease = such::ui::FrontendLease::try_acquire(such::ui::FrontendMode::Gui);
        if (!lease.acquired()) {
            MessageBoxW(nullptr,
                        L"Another Such window is already open.",
                        L"Such", MB_OK | MB_ICONINFORMATION);
            return 23;
        }
    }

    if (leaseHoldMs > 0) {
        Sleep(static_cast<DWORD>(leaseHoldMs));
        return 0;
    }


    if (!gDemo) {
        gRuntime = std::make_unique<such::runtime::RuntimeClient>();
        std::string runtimeError;
        if (!gRuntime->load(&runtimeError)) {
            // Release verification uses --smoke. A missing/broken production
            // runtime must be a hard failure in that mode; otherwise CI could
            // accept a frontend that only paints successfully. Avoid a modal
            // MessageBox in smoke mode so unattended verification cannot hang.
            if (gSmoke) return 5;
            MessageBoxW(nullptr, widen_utf8("Such index could not be loaded: " + runtimeError).c_str(), L"Such", MB_OK | MB_ICONWARNING);
            return 5;
        }
        const auto status = gRuntime->status();
        gRuntimeGeneration = status.generation;
        gRuntimeIndexedFiles = status.indexed_files;
        gRuntimeIndexing = status.indexing;
        if (!status.indexing && status.indexed_files == 0 && !status.roots.empty()) gRuntime->reindex_async();
    }

    if (gDemo) {
        gResults = such::ui::make_demo_results("", such::ui::PlatformDialect::Windows,
                                              static_cast<std::int64_t>(std::time(nullptr)));
    }

    WNDCLASSEXW wc{};
    wc.cbSize = static_cast<UINT>(sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.hInstance = instance;
    wc.lpfnWndProc = wnd_proc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return 2;

    const std::wstring windowTitle = widen_utf8(std::string(such::version::kDisplayName));
    HWND hwnd = CreateWindowExW(0, kClassName, windowTitle.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
                                CW_USEDEFAULT, CW_USEDEFAULT, 760, 600,
                                nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 3;
    // This is an interactive desktop application. Some launch paths can pass
    // SW_HIDE even though the user explicitly started Such; recover to a
    // normal visible window instead of leaving only a background process.
    ShowWindow(hwnd, show == SW_HIDE ? SW_SHOWNORMAL : show);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    if (gRuntime) {
        SetTimer(hwnd, kTimerRuntimeRefresh, 250, nullptr);
        // Keep first launch non-modal. The empty-state prompt explains that
        // Enter opens the folder picker; opening it here can strand the picker
        // behind another window and make Such look like a background process.
        if (!gSmoke && !gSmokeHold && !gDemo) {
            std::string rootsError;
            const auto roots = gRuntime->roots(&rootsError);
            if (!rootsError.empty()) {
                gSearchError = "Could not list search roots: " + rootsError;
                MessageBoxW(hwnd, widen_utf8(gSearchError).c_str(), L"Such - Search roots unavailable", MB_OK | MB_ICONWARNING);
            }
        }
    }
    if (gSmoke) SetTimer(hwnd, 1, 250, nullptr);
    if (gSmokeHold) SetTimer(hwnd, 2, 3000, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

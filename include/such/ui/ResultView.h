#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <such/ui/SearchDialect.h>

namespace such::ui {

enum class AccentTone : std::uint8_t {
    None,
    Green,
    Sand,
    Blue,
    Rose,
};

enum class IconOverride : std::uint8_t {
    SystemDefault,
    Document,
    Folder,
    Favorite,
};

enum class ContentLocatorKind : std::uint8_t {
    None = 0,
    Line = 1,
    Page = 2,
    Slide = 3,
    Sheet = 4,
    Offset = 5,
    Metadata = 6,
};

struct ResultItem {
    std::uint64_t file_id = 0;
    std::string filename;
    std::string path;
    std::string extension;
    bool pinned = false;
    bool indexed = true;
    bool authorized = true;
    AccentTone accent = AccentTone::None;
    IconOverride icon_override = IconOverride::SystemDefault;
    std::int64_t modified_unix_seconds = 0;
    std::uint64_t size_bytes = 0;

    // Populated only for /inside results.
    bool content_match = false;
    ContentLocatorKind locator_kind = ContentLocatorKind::None;
    std::uint32_t page_number = 0;
    std::uint32_t line_number = 0;
    std::uint32_t slide_number = 0;
    std::uint32_t sheet_number = 0;
    std::uint64_t byte_offset = 0;
    std::string logical_name;
    std::string snippet;
    double content_score = 0.0;
};

// Visual verification only. Production frontends must not call this unless --demo
// was explicitly supplied. Normal production startup begins with no mock results and
// waits for the canonical SearchRuntimeClient integration.
[[nodiscard]] std::vector<ResultItem> make_demo_results(
    std::string_view query,
    PlatformDialect dialect,
    std::int64_t now_unix_seconds);

} // namespace such::ui

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace such::ui {

enum class PlatformDialect : std::uint8_t {
    Windows,
    UnixLike,
};

struct DateRange {
    std::optional<std::int64_t> after_unix_seconds;
    std::optional<std::int64_t> before_unix_seconds;
};

struct SearchFilter {
    std::string text;
    std::vector<std::string> extensions;
    // Plain-text refinements introduced by /;. Each entry is applied as an
    // additional conjunctive filename/path filter after runtime ranking.
    std::vector<std::string> detail_terms;
    DateRange modified;
    bool pinned_only = false;
};

struct DetailSearch {
    std::string primary;
    std::vector<std::string> refinements;
    bool active = false;
};

struct Suggestion {
    std::string token;
    std::string label;
};

[[nodiscard]] constexpr std::string_view operator_prefix(PlatformDialect dialect) noexcept {
    return dialect == PlatformDialect::Windows ? "/" : "//";
}

// Parses the platform query dialect. The 3-argument overload recognizes a
// conservative built-in file-extension set. Runtime integrations should use the
// 4-argument overload so slash tokens are treated as extension filters only when
// the extension is actually observed/registered; unknown slash tokens fall back
// to ordinary text as required by the Such query contract.
[[nodiscard]] SearchFilter parse_search_query(
    std::string_view query,
    PlatformDialect dialect,
    std::int64_t now_unix_seconds);

[[nodiscard]] SearchFilter parse_search_query(
    std::string_view query,
    PlatformDialect dialect,
    std::int64_t now_unix_seconds,
    const std::vector<std::string>& observed_extensions);

[[nodiscard]] DetailSearch parse_detail_search(std::string_view query);

[[nodiscard]] std::vector<Suggestion> autocomplete(
    std::string_view query,
    PlatformDialect dialect,
    const std::vector<std::string>& observed_extensions);

// Rebuilds a query using the stable runtime dialect. This lets the public
// frontend accept richer syntax (for example compact numeric date ranges)
// without requiring a private-runtime ABI change.
[[nodiscard]] std::string compile_runtime_query(
    const SearchFilter& filter,
    PlatformDialect dialect);

} // namespace such::ui

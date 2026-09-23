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

enum class SearchScope : std::uint8_t {
    File,
    Content,
};

struct DateRange {
    std::optional<std::int64_t> after_unix_seconds;
    std::optional<std::int64_t> before_unix_seconds;
};

struct SearchFilter {
    std::string text;
    std::vector<std::string> extensions;
    // Plain-text refinements introduced by /;. Each entry is applied as an
    // additional conjunctive filename/path filter after runtime ranking for
    // legacy file-only searches. RuntimeClient builds a staged plan when the
    // final refinement enters Content scope.
    std::vector<std::string> detail_terms;
    DateRange modified;
    bool pinned_only = false;
    SearchScope scope = SearchScope::File;
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

// /inside is a universal single-slash scope operator on every platform. Linux
// and other Unix-like frontends retain // for filesystem/index operators so
// absolute paths remain unambiguous.
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

// Scope itself is not serialized into the file-search dialect. RuntimeClient
// dispatches Content scope through the optional content ABI.
[[nodiscard]] std::string compile_runtime_query(
    const SearchFilter& filter,
    PlatformDialect dialect);

} // namespace such::ui

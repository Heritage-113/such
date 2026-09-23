#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace such::runtime {

enum class IndexPolicyOverride : unsigned char {
    None,
    ExplicitCurrentUserAppData,
};

struct IndexPathPolicyPlan {
    bool root_allowed = true;
    std::string rejection_reason;
    std::vector<std::filesystem::path> exclusions;
};

// Conventional high-churn dependency/cache directories. These are excluded as
// subtrees before the private runtime starts a crawl so they never consume
// parser, index, or content-search bandwidth.
[[nodiscard]] bool is_default_noise_directory_name(std::string_view name) noexcept;

// Pure string classifier used by Windows builds and contract tests. It does not
// inspect or read the path. The rule is case-insensitive and component-aware.
[[nodiscard]] bool is_windows_protected_index_path(std::string_view utf8_path);

// Exact search-box aliases that represent explicit user intent to index the
// current user's AppData tree. Merely having an AppData path under a normal root
// never enables this override.
[[nodiscard]] bool is_windows_manual_index_alias(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::filesystem::path> resolve_windows_manual_index_alias(std::string_view text);
[[nodiscard]] bool is_current_user_manual_appdata_root(const std::filesystem::path& root);

// Builds concrete directory exclusions for a root. On Windows this includes
// protected OS/application-data directories. On every desktop platform it also
// discovers .git, node_modules and __pycache__ directories without entering
// those subtrees.
[[nodiscard]] IndexPathPolicyPlan plan_default_index_policy(
    const std::filesystem::path& root,
    bool discover_noise_directories = true,
    IndexPolicyOverride override_mode = IndexPolicyOverride::None);

} // namespace such::runtime

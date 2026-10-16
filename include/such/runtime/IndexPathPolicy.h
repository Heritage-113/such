#pragma once

#include <filesystem>
#include <optional>
#include <stop_token>
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

struct IndexPolicyRootRequest {
    std::filesystem::path root;
    IndexPolicyOverride override_mode = IndexPolicyOverride::None;
};

// Conventional high-churn dependency/cache directories. Product root mutation
// paths identify these through deferred, bounded runtime-index maintenance so
// selecting a drive never triggers a synchronous recursive pre-scan. Explicit
// recursive discovery remains available only for tests/offline tooling.
[[nodiscard]] bool is_default_noise_directory_name(std::string_view name) noexcept;
[[nodiscard]] std::optional<std::string> default_noise_directory_prefix(
    std::string_view utf8_path,
    std::string_view noise_name);


// Pure string classifier used by Windows builds and contract tests. It does not
// inspect or read the path. The rule is case-insensitive and component-aware.
[[nodiscard]] bool is_windows_protected_index_path(std::string_view utf8_path);

// Installed Such path (running Inno install, or the default per-user path).
// Default: %LOCALAPPDATA%\\Programs\\Such. The directory and descendants are the
// highest-priority inclusion; sibling system/AppData content remains excluded.
[[nodiscard]] std::filesystem::path windows_such_install_root();
[[nodiscard]] bool is_windows_such_install_path(std::string_view utf8_path) noexcept;

// Exact search-box aliases that represent explicit user intent to index the
// current user's AppData tree. Merely having an AppData path under a normal root
// never enables this override.
[[nodiscard]] bool is_windows_manual_index_alias(std::string_view text) noexcept;
[[nodiscard]] std::optional<std::filesystem::path> resolve_windows_manual_index_alias(std::string_view text);
[[nodiscard]] bool is_current_user_manual_appdata_root(const std::filesystem::path& root);

// Explicit recursive discovery helper for tests/offline tooling. Product root
// mutation paths must not call this on the user-interaction thread.
[[nodiscard]] std::vector<std::filesystem::path> discover_default_noise_exclusions(
    const std::filesystem::path& root,
    IndexPolicyOverride override_mode = IndexPolicyOverride::None,
    std::stop_token stop_token = {});

[[nodiscard]] IndexPathPolicyPlan plan_default_index_policy(
    const std::filesystem::path& root,
    bool discover_noise_directories = false,
    IndexPolicyOverride override_mode = IndexPolicyOverride::None);

// Compile policy for a complete root set. Explicit AppData roots are global
// exceptions: exclusions contributed by a broader root may not cover them.
// This is the planner used by root replacement/reseed operations.
[[nodiscard]] IndexPathPolicyPlan plan_default_index_policy_set(
    const std::vector<IndexPolicyRootRequest>& roots,
    bool discover_noise_directories = false);

} // namespace such::runtime

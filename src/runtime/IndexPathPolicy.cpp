#include <such/runtime/IndexPathPolicy.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>
#include <unordered_set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#endif

namespace such::runtime {
namespace {

#if defined(_WIN32)
std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
#endif

std::string path_key(const std::filesystem::path& path) {
    auto text = path.lexically_normal().generic_string();
#if defined(_WIN32)
    text = lower_ascii(std::move(text));
#endif
    while (text.size() > 1u && text.back() == '/') text.pop_back();
    return text;
}

bool key_is_within(std::string_view candidate, std::string_view parent) {
    if (candidate == parent) return true;
    if (parent.empty() || candidate.size() <= parent.size()) return false;
    if (!candidate.starts_with(parent)) return false;
    return parent.back() == '/' || candidate[parent.size()] == '/';
}

bool path_is_within(const std::filesystem::path& candidate, const std::filesystem::path& parent) {
    return key_is_within(path_key(candidate), path_key(parent));
}

std::filesystem::path normalized_absolute(const std::filesystem::path& input) {
    std::error_code ec;
    auto value = std::filesystem::absolute(input, ec);
    if (ec) value = input;
    return value.lexically_normal();
}

void append_unique(std::vector<std::filesystem::path>& out,
                   std::unordered_set<std::string>& seen,
                   const std::filesystem::path& value) {
    const auto normalized = normalized_absolute(value);
    const auto key = path_key(normalized);
    if (!key.empty() && seen.insert(key).second) out.push_back(normalized);
}

bool component_equal_ascii(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto a = static_cast<unsigned char>(lhs[i]);
        const auto b = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

std::vector<std::string> split_windows_components(std::string_view input) {
    std::string normalized(input);
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < normalized.size()) {
        while (start < normalized.size() && normalized[start] == '\\') ++start;
        if (start >= normalized.size()) break;
        const auto end = normalized.find('\\', start);
        parts.emplace_back(normalized.substr(start, end == std::string::npos ? normalized.size() - start : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return parts;
}

#if defined(_WIN32)
std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    if (SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value) != S_OK || value == nullptr) return {};
    std::filesystem::path out(value);
    CoTaskMemFree(value);
    return out;
}

std::vector<std::filesystem::path> windows_fixed_exclusions() {
    std::vector<std::filesystem::path> out;
    std::unordered_set<std::string> seen;

    // Known-folder APIs cover redirected enterprise profiles without relying on
    // APPDATA/LOCALAPPDATA environment variables.
    const std::array<KNOWNFOLDERID, 6> known{{
        FOLDERID_RoamingAppData,
        FOLDERID_LocalAppData,
        FOLDERID_LocalAppDataLow,
        FOLDERID_ProgramData,
        FOLDERID_ProgramFiles,
        FOLDERID_ProgramFilesX86,
    }};
    for (const auto& id : known) {
        const auto path = known_folder(id);
        if (!path.empty()) append_unique(out, seen, path);
    }

    // C: operating-system/service trees are noise for a user document search
    // engine and can be extremely hot. Keep them out of the traversal entirely.
    const std::array<std::wstring_view, 13> c_drive{{
        L"C:\\Windows",
        L"C:\\Windows.old",
        L"C:\\Program Files",
        L"C:\\Program Files (x86)",
        L"C:\\Program Files (Arm)",
        L"C:\\ProgramData",
        L"C:\\System Volume Information",
        L"C:\\$Recycle.Bin",
        L"C:\\Recovery",
        L"C:\\PerfLogs",
        L"C:\\$WinREAgent",
        L"C:\\$Windows.~BT",
        L"C:\\$Windows.~WS",
    }};
    for (const auto value : c_drive) append_unique(out, seen, std::filesystem::path(value));

    // Exclude AppData for every local profile visible to the current process,
    // including profiles other than the current user. No contents are read.
    auto profiles = known_folder(FOLDERID_UserProfiles);
    if (profiles.empty()) profiles = std::filesystem::path(L"C:\\Users");
    std::error_code ec;
    for (std::filesystem::directory_iterator it(profiles, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_directory(type_ec) || type_ec) continue;
        const auto appdata = it->path() / L"AppData";
        std::error_code exists_ec;
        if (std::filesystem::is_directory(appdata, exists_ec) && !exists_ec) append_unique(out, seen, appdata);
    }
    return out;
}
#endif

bool should_skip_fixed(const std::filesystem::path& path,
                       const std::vector<std::filesystem::path>& fixed) {
    return std::any_of(fixed.begin(), fixed.end(), [&](const auto& excluded) {
        return path_is_within(path, excluded);
    });
}

} // namespace

bool is_default_noise_directory_name(std::string_view name) noexcept {
    return component_equal_ascii(name, "node_modules") ||
           component_equal_ascii(name, ".git") ||
           component_equal_ascii(name, "__pycache__");
}

bool is_windows_manual_index_alias(std::string_view text) noexcept {
    return component_equal_ascii(text, "%APPDATA%") ||
           component_equal_ascii(text, "%LOCALAPPDATA%");
}

std::optional<std::filesystem::path> resolve_windows_manual_index_alias(std::string_view text) {
#if defined(_WIN32)
    if (component_equal_ascii(text, "%APPDATA%")) {
        const auto path = known_folder(FOLDERID_RoamingAppData);
        if (!path.empty()) return normalized_absolute(path);
    }
    if (component_equal_ascii(text, "%LOCALAPPDATA%")) {
        const auto path = known_folder(FOLDERID_LocalAppData);
        if (!path.empty()) return normalized_absolute(path);
    }
#else
    (void)text;
#endif
    return std::nullopt;
}

bool is_current_user_manual_appdata_root(const std::filesystem::path& root) {
#if defined(_WIN32)
    const auto normalized = normalized_absolute(root);
    const auto roaming = known_folder(FOLDERID_RoamingAppData);
    const auto local = known_folder(FOLDERID_LocalAppData);
    return (!roaming.empty() && path_key(normalized) == path_key(normalized_absolute(roaming))) ||
           (!local.empty() && path_key(normalized) == path_key(normalized_absolute(local)));
#else
    (void)root;
    return false;
#endif
}

bool is_windows_protected_index_path(std::string_view utf8_path) {
    const auto parts = split_windows_components(utf8_path);
    if (parts.empty()) return false;

    for (const auto& part : parts) {
        if (component_equal_ascii(part, "AppData")) return true;
        if (is_default_noise_directory_name(part)) return true;
    }

    // Treat only C: as the fixed OS drive here. Redirected known folders are
    // handled by SHGetKnownFolderPath in the concrete Windows planner.
    if (!component_equal_ascii(parts.front(), "C:")) return false;
    if (parts.size() < 2u) return false;
    const std::string& first = parts[1];
    const std::array<std::string_view, 13> protected_names{{
        "Windows", "Windows.old", "Program Files", "Program Files (x86)",
        "Program Files (Arm)", "ProgramData", "System Volume Information",
        "$Recycle.Bin", "Recovery", "PerfLogs", "$WinREAgent",
        "$Windows.~BT", "$Windows.~WS",
    }};
    return std::any_of(protected_names.begin(), protected_names.end(), [&](std::string_view name) {
        return component_equal_ascii(first, name);
    });
}

IndexPathPolicyPlan plan_default_index_policy(const std::filesystem::path& root,
                                                   bool discover_noise_directories,
                                                   IndexPolicyOverride override_mode) {
    IndexPathPolicyPlan plan;
    if (root.empty()) {
        plan.root_allowed = false;
        plan.rejection_reason = "Search root is empty";
        return plan;
    }

    const auto normalized_root = normalized_absolute(root);
    const auto root_name = normalized_root.filename().string();
    if (is_default_noise_directory_name(root_name)) {
        plan.root_allowed = false;
        plan.rejection_reason = "Such does not index dependency/cache directories by default: " + root_name;
        return plan;
    }

    std::vector<std::filesystem::path> fixed;
    std::unordered_set<std::string> seen;
#if defined(_WIN32)
    const bool explicit_appdata =
        override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData &&
        is_current_user_manual_appdata_root(normalized_root);
    if (override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData && !explicit_appdata) {
        plan.root_allowed = false;
        plan.rejection_reason = "Explicit AppData override is valid only for the current user's AppData known folders";
        return plan;
    }
    if (!explicit_appdata && is_windows_protected_index_path(path_key(normalized_root))) {
        plan.root_allowed = false;
        plan.rejection_reason = "Such does not index Windows/AppData system trees by default";
        return plan;
    }
    fixed = windows_fixed_exclusions();
    if (explicit_appdata) {
        // Remove only fixed exclusions that are ancestors of the explicitly
        // selected AppData root. Sibling/system exclusions remain intact.
        fixed.erase(std::remove_if(fixed.begin(), fixed.end(), [&](const auto& excluded) {
            return path_is_within(normalized_root, excluded);
        }), fixed.end());
    }
    for (const auto& excluded : fixed) {
        if (path_is_within(normalized_root, excluded)) {
            plan.root_allowed = false;
            plan.rejection_reason = "Such does not index Windows/AppData system trees by default";
            return plan;
        }
        if (path_is_within(excluded, normalized_root)) append_unique(plan.exclusions, seen, excluded);
    }
#else
    (void)override_mode;
#endif

    if (!discover_noise_directories) return plan;

    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(normalized_root, options, ec), end;
    while (!ec && it != end) {
        const auto path = it->path();
        std::error_code symlink_ec;
        const bool symlink = it->is_symlink(symlink_ec) && !symlink_ec;
        std::error_code dir_ec;
        const bool directory = it->is_directory(dir_ec) && !dir_ec;
        if (directory) {
            const auto name = path.filename().string();
            const bool fixed_excluded = should_skip_fixed(path, fixed);
            const bool noisy = is_default_noise_directory_name(name);
#if defined(_WIN32)
            const bool protected_path =
                !(override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData &&
                  is_current_user_manual_appdata_root(normalized_root)) &&
                is_windows_protected_index_path(path_key(path));
#else
            const bool protected_path = false;
#endif
            if (fixed_excluded || noisy || protected_path || symlink) {
                if (!symlink && (fixed_excluded || noisy || protected_path)) append_unique(plan.exclusions, seen, path);
                it.disable_recursion_pending();
            }
        }
        it.increment(ec);
        if (ec == std::errc::permission_denied) ec.clear();
    }
    return plan;
}

} // namespace such::runtime

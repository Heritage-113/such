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
using PathKey = std::wstring;

PathKey path_key(const std::filesystem::path& path) {
    PathKey text = path.lexically_normal().generic_wstring();
    while (text.size() > 1u && text.back() == L'/') text.pop_back();
    if (text.empty()) return text;

    // Windows paths are case-insensitive. Fold with the invariant Windows
    // Unicode mapper instead of lowercasing UTF-8 bytes as ASCII; the latter
    // incorrectly treated case variants of non-ASCII user/profile names as
    // distinct roots.
    const int source_length = static_cast<int>(text.size());
    const int folded_length = LCMapStringEx(
        LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), source_length,
        nullptr, 0, nullptr, nullptr, 0);
    if (folded_length <= 0) return text;
    PathKey folded(static_cast<std::size_t>(folded_length), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.data(), source_length,
                      folded.data(), folded_length, nullptr, nullptr, 0) <= 0) {
        return text;
    }
    return folded;
}
#else
using PathKey = std::string;

PathKey path_key(const std::filesystem::path& path) {
    PathKey text = path.lexically_normal().generic_string();
    while (text.size() > 1u && text.back() == '/') text.pop_back();
    return text;
}
#endif

std::string path_utf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
    return path.string();
#endif
}

bool key_is_within(const PathKey& candidate, const PathKey& parent) {
    if (candidate == parent) return true;
    if (parent.empty() || candidate.size() <= parent.size()) return false;
    if (!candidate.starts_with(parent)) return false;
#if defined(_WIN32)
    return parent.back() == L'/' || candidate[parent.size()] == L'/';
#else
    return parent.back() == '/' || candidate[parent.size()] == '/';
#endif
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
                   std::unordered_set<PathKey>& seen,
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

bool has_such_install_component_chain(const std::vector<std::string>& parts) {
    if (parts.size() < 4u) return false;
    for (std::size_t i = 0; i + 3u < parts.size(); ++i) {
        if (component_equal_ascii(parts[i], "AppData") &&
            component_equal_ascii(parts[i + 1u], "Local") &&
            component_equal_ascii(parts[i + 2u], "Programs") &&
            component_equal_ascii(parts[i + 3u], "Such")) {
            return true;
        }
    }
    return false;
}

#if defined(_WIN32)
std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    const HRESULT result = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value);
    if (FAILED(result) || value == nullptr) return {};
    std::filesystem::path out(value);
    CoTaskMemFree(value);
    return out;
}

void append_children_except(
    std::vector<std::filesystem::path>& out,
    std::unordered_set<PathKey>& seen,
    const std::filesystem::path& parent,
    const std::filesystem::path& allowed_child) {
    std::error_code ec;
    const auto allowed_key = path_key(normalized_absolute(allowed_child));
    for (std::filesystem::directory_iterator it(
             parent, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        // The runtime canonicalizes excludes. Excluding a junction such as
        // Local/Application Data would otherwise exclude Local itself.
        const DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) continue;
        const auto candidate = normalized_absolute(it->path());
        if (path_key(candidate) == allowed_key) continue;
        append_unique(out, seen, candidate);
    }
}

std::vector<std::filesystem::path> windows_fixed_exclusions(
    const std::filesystem::path& scope_root) {
    std::vector<std::filesystem::path> out;
    std::unordered_set<PathKey> seen;
    const auto scope = normalized_absolute(scope_root);

    auto append_if_inside_scope = [&](const std::filesystem::path& candidate) {
        if (!candidate.empty() && path_is_within(normalized_absolute(candidate), scope)) {
            append_unique(out, seen, candidate);
        }
    };

    // System/application roots remain hard exclusions, but only material that
    // can actually occur below this selected root is planned. In particular,
    // choosing a D:/ or project root must not synchronously enumerate C:/Users.
    const std::array<KNOWNFOLDERID, 3> known{{
        FOLDERID_ProgramData,
        FOLDERID_ProgramFiles,
        FOLDERID_ProgramFilesX86,
    }};
    for (const auto& id : known) append_if_inside_scope(known_folder(id));

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
    for (const auto value : c_drive) append_if_inside_scope(std::filesystem::path(value));

    const auto current_profile = known_folder(FOLDERID_Profile);
    const auto roaming_appdata = known_folder(FOLDERID_RoamingAppData);
    const auto local_appdata = known_folder(FOLDERID_LocalAppData);
    const auto local_low_appdata = known_folder(FOLDERID_LocalAppDataLow);
    const auto install_root = windows_such_install_root();

    append_if_inside_scope(roaming_appdata);
    append_if_inside_scope(local_low_appdata);

    // LocalAppData cannot be excluded as one parent because the private ABI's
    // exclusion state is parent-dominant: a later set_indexed(child, true) does
    // not reopen a child of an excluded parent. Therefore, only when the chosen
    // root contains LocalAppData, seed its immediate siblings while preserving
    // Programs/Such. This is bounded one-level enumeration, never a selected-
    // drive recursive walk.
    if (!local_appdata.empty() && path_is_within(local_appdata, scope)) {
        const auto programs = normalized_absolute(local_appdata / L"Programs");
        append_children_except(out, seen, local_appdata, programs);
        std::error_code programs_ec;
        if (std::filesystem::is_directory(programs, programs_ec) && !programs_ec) {
            append_children_except(out, seen, programs, install_root);
        }
    }

    // Other profiles' complete AppData trees remain excluded. Avoid touching
    // the profile directory at all when it cannot intersect the selected root.
    auto profiles = known_folder(FOLDERID_UserProfiles);
    if (profiles.empty()) profiles = std::filesystem::path(L"C:\\Users");
    const bool profiles_intersect_scope =
        path_is_within(profiles, scope) || path_is_within(scope, profiles);
    if (profiles_intersect_scope) {
        std::error_code profile_ec;
        for (std::filesystem::directory_iterator it(
                 profiles, std::filesystem::directory_options::skip_permission_denied, profile_ec), end;
             !profile_ec && it != end; it.increment(profile_ec)) {
            std::error_code type_ec;
            if (!it->is_directory(type_ec) || type_ec) continue;
            const auto profile = normalized_absolute(it->path());
            if (!current_profile.empty() &&
                path_key(profile) == path_key(normalized_absolute(current_profile))) {
                continue;
            }
            const auto appdata = profile / L"AppData";
            std::error_code exists_ec;
            if (std::filesystem::is_directory(appdata, exists_ec) && !exists_ec) {
                append_if_inside_scope(appdata);
            }
        }
    }
    // Exclusions are parent-dominant in ABI v1. Preserve the exact installed
    // subtree by splitting any blocked ancestor into its off-path siblings.
    // Never unblock the entire system/AppData parent to admit Such.
    if (!install_root.empty()) {
        auto original = std::move(out);
        out.clear();
        seen.clear();
        for (const auto& excluded : original) {
            if (path_is_within(excluded, install_root)) continue;
            if (!path_is_within(install_root, excluded)) {
                append_unique(out, seen, excluded);
                continue;
            }
            auto cursor = install_root;
            while (path_key(cursor) != path_key(excluded)) {
                const auto parent = cursor.parent_path();
                if (parent.empty() || path_key(parent) == path_key(cursor)) break;
                append_children_except(out, seen, parent, cursor);
                cursor = parent;
            }
        }
    }
    return out;
}
#endif

#if defined(_WIN32)
bool is_current_user_such_install_path(const std::filesystem::path& path) {
    const auto install_root = windows_such_install_root();
    return !install_root.empty() && path_is_within(normalized_absolute(path), install_root);
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


std::optional<std::string> default_noise_directory_prefix(
    std::string_view utf8_path,
    std::string_view noise_name) {
    if (!is_default_noise_directory_name(noise_name)) return std::nullopt;

    auto component_equal_platform = [](std::string_view lhs, std::string_view rhs) {
        if (lhs.size() != rhs.size()) return false;
        for (std::size_t i = 0; i < lhs.size(); ++i) {
#if defined(_WIN32)
            if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
                std::tolower(static_cast<unsigned char>(rhs[i]))) return false;
#else
            if (lhs[i] != rhs[i]) return false;
#endif
        }
        return true;
    };

    std::size_t start = 0;
    while (start < utf8_path.size()) {
        while (start < utf8_path.size() &&
               (utf8_path[start] == '/' || utf8_path[start] == '\\')) {
            ++start;
        }
        if (start >= utf8_path.size()) break;
        std::size_t end = start;
        while (end < utf8_path.size() &&
               utf8_path[end] != '/' && utf8_path[end] != '\\') {
            ++end;
        }
        if (component_equal_platform(utf8_path.substr(start, end - start), noise_name)) {
            return std::string(utf8_path.substr(0, end));
        }
        start = end + 1u;
    }
    return std::nullopt;
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

std::filesystem::path windows_such_install_root() {
#if defined(_WIN32)
    // Resolve the running installed copy, including a CLI in its cli subfolder.
    // Build-tree/test executables have no adjacent Inno uninstaller and retain
    // the standard per-user path below.
    std::wstring module(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length > 0 && length < module.size()) {
        module.resize(length);
        auto directory = std::filesystem::path(module).parent_path();
        if (component_equal_ascii(path_utf8(directory.filename()), "cli")) directory = directory.parent_path();
        std::error_code ec;
        if (std::filesystem::is_regular_file(directory / L"unins000.exe", ec) && !ec) {
            return normalized_absolute(directory);
        }
    }
    const auto local = known_folder(FOLDERID_LocalAppData);
    if (local.empty()) return {};
    return normalized_absolute(local / L"Programs" / L"Such");
#else
    return {};
#endif
}

bool is_windows_such_install_path(std::string_view utf8_path) noexcept {
    const auto parts = split_windows_components(utf8_path);
    return has_such_install_component_chain(parts);
}

bool is_windows_protected_index_path(std::string_view utf8_path) {
    const auto parts = split_windows_components(utf8_path);
    if (parts.empty()) return false;

    for (const auto& part : parts) {
        if (is_default_noise_directory_name(part)) return true;
    }
    for (const auto& part : parts) {
        if (component_equal_ascii(part, "AppData")) return true;
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


std::vector<std::filesystem::path> discover_default_noise_exclusions(
    const std::filesystem::path& root,
    IndexPolicyOverride override_mode,
    std::stop_token stop_token) {
    std::vector<std::filesystem::path> discovered;
    std::unordered_set<PathKey> seen;
    if (root.empty()) return discovered;
#if defined(_WIN32)
    if (is_current_user_such_install_path(root)) return discovered;
    const auto install_root = windows_such_install_root();
#endif
    const auto normalized_root = normalized_absolute(root);
    std::vector<std::filesystem::path> fixed;
#if defined(_WIN32)
    fixed = windows_fixed_exclusions(normalized_root);
    const bool explicit_appdata =
        override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData &&
        is_current_user_manual_appdata_root(normalized_root);
    if (explicit_appdata) {
        fixed.erase(std::remove_if(fixed.begin(), fixed.end(), [&](const auto& excluded) {
            return path_is_within(normalized_root, excluded) ||
                   path_is_within(excluded, normalized_root);
        }), fixed.end());
    }
#else
    (void)override_mode;
#endif

    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator it(normalized_root, options, ec), end;
    while (!ec && it != end) {
        if (stop_token.stop_requested()) break;

        const auto current_path = it->path();
        std::error_code symlink_ec;
        const bool symlink = it->is_symlink(symlink_ec) && !symlink_ec;
        std::error_code dir_ec;
        const bool directory = it->is_directory(dir_ec) && !dir_ec;
        if (directory) {
            const auto name = path_utf8(current_path.filename());
            const bool fixed_excluded = should_skip_fixed(current_path, fixed);
            const bool noisy = is_default_noise_directory_name(name)
#if defined(_WIN32)
                && !is_current_user_such_install_path(current_path)
                && (install_root.empty() || !path_is_within(install_root, current_path))
#endif
                ;
#if defined(_WIN32)
            const bool protected_path =
                !(override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData &&
                  is_current_user_manual_appdata_root(normalized_root)) &&
                !is_current_user_such_install_path(current_path) &&
                (install_root.empty() || !path_is_within(install_root, current_path)) &&
                is_windows_protected_index_path(path_utf8(current_path));
#else
            const bool protected_path = false;
#endif
            if (fixed_excluded || noisy || protected_path || symlink) {
                if (!symlink && !fixed_excluded && (noisy || protected_path)) {
                    append_unique(discovered, seen, current_path);
                }
                it.disable_recursion_pending();
            }
        }

        it.increment(ec);
        if (ec == std::errc::permission_denied) ec.clear();
    }
    return discovered;
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
#if defined(_WIN32)
    // The installed subtree wins over every generic protected/noise rule.
    if (is_current_user_such_install_path(normalized_root)) return plan;
#endif
    const auto root_name = path_utf8(normalized_root.filename());
    if (is_default_noise_directory_name(root_name)) {
        plan.root_allowed = false;
        plan.rejection_reason = "Such does not index dependency/cache directories by default: " + root_name;
        return plan;
    }

    std::vector<std::filesystem::path> fixed;
    std::unordered_set<PathKey> seen;
#if defined(_WIN32)
    const bool explicit_appdata =
        override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData &&
        is_current_user_manual_appdata_root(normalized_root);
    if (override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData && !explicit_appdata) {
        plan.root_allowed = false;
        plan.rejection_reason = "Explicit AppData override is valid only for the current user's AppData known folders";
        return plan;
    }
    const bool current_install_path = is_current_user_such_install_path(normalized_root);
    if (!explicit_appdata && !current_install_path &&
        is_windows_protected_index_path(path_utf8(normalized_root))) {
        plan.root_allowed = false;
        plan.rejection_reason = "Such does not index Windows/AppData system trees by default";
        return plan;
    }
    fixed = windows_fixed_exclusions(normalized_root);
    if (explicit_appdata) {
        // Explicit AppData selection is authoritative for that known-folder root.
        // Remove fixed AppData exclusions that are either ancestors or descendants
        // of the selected root; unrelated system exclusions remain intact.
        fixed.erase(std::remove_if(fixed.begin(), fixed.end(), [&](const auto& excluded) {
            return path_is_within(normalized_root, excluded) ||
                   path_is_within(excluded, normalized_root);
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

    for (const auto& excluded : discover_default_noise_exclusions(
             normalized_root, override_mode)) {
        append_unique(plan.exclusions, seen, excluded);
    }
    return plan;
}

IndexPathPolicyPlan plan_default_index_policy_set(
    const std::vector<IndexPolicyRootRequest>& roots,
    bool discover_noise_directories) {
    IndexPathPolicyPlan combined;
    std::unordered_set<PathKey> seen;
    std::vector<std::filesystem::path> explicit_appdata_roots;

    for (const auto& request : roots) {
        auto item = plan_default_index_policy(
            request.root, discover_noise_directories, request.override_mode);
        if (!item.root_allowed) return item;
        for (const auto& excluded : item.exclusions) {
            append_unique(combined.exclusions, seen, excluded);
        }
#if defined(_WIN32)
        if (request.override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData &&
            is_current_user_manual_appdata_root(request.root)) {
            explicit_appdata_roots.push_back(normalized_absolute(request.root));
        }
#endif
    }

#if defined(_WIN32)
    if (!explicit_appdata_roots.empty()) {
        combined.exclusions.erase(
            std::remove_if(combined.exclusions.begin(), combined.exclusions.end(),
                [&](const auto& excluded) {
                    return std::any_of(explicit_appdata_roots.begin(), explicit_appdata_roots.end(),
                        [&](const auto& allowed_root) {
                            return path_is_within(excluded, allowed_root) ||
                                   path_is_within(allowed_root, excluded);
                        });
                }),
            combined.exclusions.end());
    }
#endif
    return combined;
}

} // namespace such::runtime

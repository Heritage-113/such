#include <such/runtime/IndexRootService.h>

#include <such/runtime/IndexPathPolicy.h>
#include <such/runtime/RuntimeClient.h>

#include <algorithm>
#include <array>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace such::runtime {
namespace {

struct ResolvedRoot {
    std::filesystem::path path;
    IndexPolicyOverride override_mode = IndexPolicyOverride::None;
};

struct PreparedRootSet {
    std::vector<ResolvedRoot> roots;
    std::vector<std::filesystem::path> exclusions;
};

std::string path_to_utf8(const std::filesystem::path& file_path) {
#if defined(_WIN32)
    const auto utf8_path = file_path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8_path.data()), utf8_path.size());
#else
    return file_path.string();
#endif
}

std::filesystem::path path_from_utf8(std::string_view utf8_path) {
#if defined(_WIN32)
    if (utf8_path.empty()) return {};
    const int source_length = static_cast<int>(utf8_path.size());
    const int wide_length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path.data(), source_length, nullptr, 0);
    if (wide_length <= 0) return {};
    std::wstring wide_path(static_cast<std::size_t>(wide_length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path.data(), source_length,
                            wide_path.data(), wide_length) <= 0) {
        return {};
    }
    return std::filesystem::path(wide_path);
#else
    return std::filesystem::path(utf8_path);
#endif
}

#if defined(_WIN32)
using ComparablePathKey = std::wstring;

ComparablePathKey comparable_path_key(const std::filesystem::path& file_path) {
    ComparablePathKey key = file_path.lexically_normal().generic_wstring();
    while (key.size() > 1u && key.back() == L'/') key.pop_back();
    if (key.empty()) return key;
    const int source_length = static_cast<int>(key.size());
    const int folded_length = LCMapStringEx(
        LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, key.data(), source_length,
        nullptr, 0, nullptr, nullptr, 0);
    if (folded_length <= 0) return key;
    ComparablePathKey folded(static_cast<std::size_t>(folded_length), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, key.data(), source_length,
                      folded.data(), folded_length, nullptr, nullptr, 0) <= 0) {
        return key;
    }
    return folded;
}
#else
using ComparablePathKey = std::string;

ComparablePathKey comparable_path_key(const std::filesystem::path& file_path) {
    auto key = file_path.lexically_normal().generic_string();
    while (key.size() > 1u && key.back() == '/') key.pop_back();
    return key;
}
#endif

ResolvedRoot resolve_root_request(const std::filesystem::path& requested) {
#if defined(_WIN32)
    const auto raw = path_to_utf8(requested);
    if (is_windows_manual_index_alias(raw)) {
        if (const auto resolved = resolve_windows_manual_index_alias(raw)) {
            return {*resolved, IndexPolicyOverride::ExplicitCurrentUserAppData};
        }
    }
#endif
    return {requested, IndexPolicyOverride::None};
}

ResolvedRoot resolve_persisted_root(const std::filesystem::path& persisted) {
#if defined(_WIN32)
    // Exact AppData roots can only be admitted through an explicit user override.
    // Reconstruct that intent after restart so maintenance cannot hide a root
    // that Such itself previously accepted.
    if (is_current_user_manual_appdata_root(persisted)) {
        return {persisted, IndexPolicyOverride::ExplicitCurrentUserAppData};
    }
#endif
    return {persisted, IndexPolicyOverride::None};
}

bool prepare_root_set(std::vector<ResolvedRoot> resolved_roots,
                      PreparedRootSet& prepared,
                      std::string* error) {
    std::vector<ResolvedRoot> unique_roots;
    unique_roots.reserve(resolved_roots.size());
    for (auto& root : resolved_roots) {
        const auto key = comparable_path_key(root.path);
        const auto existing = std::find_if(unique_roots.begin(), unique_roots.end(), [&](const auto& value) {
            return comparable_path_key(value.path) == key;
        });
        if (existing == unique_roots.end()) {
            unique_roots.push_back(std::move(root));
        } else if (root.override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData) {
            existing->override_mode = root.override_mode;
        }
    }

    std::vector<IndexPolicyRootRequest> policy_requests;
    policy_requests.reserve(unique_roots.size());
    for (const auto& root : unique_roots) {
        policy_requests.push_back({root.path, root.override_mode});
    }

    // Responsiveness invariant: product root changes use bounded planning only.
    // No recursive traversal of the selected storage happens here.
    auto plan = plan_default_index_policy_set(policy_requests, false);
    if (!plan.root_allowed) {
        if (error != nullptr) {
            *error = plan.rejection_reason.empty()
                ? "Search root is blocked by the default index policy"
                : plan.rejection_reason;
        }
        return false;
    }

    prepared.roots = std::move(unique_roots);
    prepared.exclusions = std::move(plan.exclusions);
    return true;
}

bool apply_exclusions(RuntimeClient& runtime,
                      const std::vector<std::filesystem::path>& exclusions,
                      std::string* error) {
    for (const auto& excluded_path : exclusions) {
        const auto excluded_utf8 = path_to_utf8(excluded_path);
        std::string mutation_error;
        if (!runtime.set_indexed(excluded_utf8, false, &mutation_error)) {
            if (error != nullptr) {
                *error = mutation_error.empty()
                    ? "Such could not apply a default index exclusion: " + excluded_utf8
                    : mutation_error;
            }
            return false;
        }
    }
    return true;
}

std::vector<ResolvedRoot> resolve_persisted_roots(const std::vector<std::string>& root_values) {
    std::vector<ResolvedRoot> resolved;
    resolved.reserve(root_values.size());
    for (const auto& root_value : root_values) {
        const auto root_path = path_from_utf8(root_value);
        if (!root_value.empty() && root_path.empty()) continue;
        resolved.push_back(resolve_persisted_root(root_path));
    }
    return resolved;
}

} // namespace

struct IndexRootService::Impl {
    RuntimeClient& runtime;
    such::ui::PlatformDialect dialect;
    bool maintenance_enabled = true;
    bool maintenance_pending = false;
    bool maintenance_fixed_ready = false;
    std::size_t maintenance_fixed_index = 0;
    std::size_t noise_term_index = 0;
    std::vector<std::filesystem::path> maintenance_fixed_exclusions;
    std::array<std::unordered_set<std::string>, 3> maintenance_seen_prefixes;

    explicit Impl(RuntimeClient& runtime_value,
                  such::ui::PlatformDialect dialect_value,
                  bool enable_maintenance)
        : runtime(runtime_value),
          dialect(dialect_value),
          maintenance_enabled(enable_maintenance) {}

    bool current_resolved_roots(std::vector<ResolvedRoot>& resolved, std::string* error) {
        std::string roots_error;
        const auto root_values = runtime.roots(&roots_error);
        if (!roots_error.empty()) {
            if (error != nullptr) *error = roots_error;
            return false;
        }
        resolved = resolve_persisted_roots(root_values);
        return true;
    }

    bool prepare_requested_roots(const std::vector<std::filesystem::path>& requested,
                                 PreparedRootSet& prepared,
                                 std::string* error) {
        std::vector<ResolvedRoot> resolved;
        resolved.reserve(requested.size());
        for (const auto& root : requested) resolved.push_back(resolve_root_request(root));
        return prepare_root_set(std::move(resolved), prepared, error);
    }

    bool apply_prepared_root_set(const PreparedRootSet& prepared, std::string* error) {
        // Runtime semantics are parent-dominant and replace_roots() clears the
        // per-path exclusion table. Therefore the only safe full replacement is
        // reset -> seed exclusions -> add roots. add_root() preserves exclusions
        // while it schedules the private asynchronous crawl.
        std::string mutation_error;
        if (!runtime.replace_roots({}, &mutation_error)) {
            if (error != nullptr) *error = mutation_error;
            return false;
        }
        if (!apply_exclusions(runtime, prepared.exclusions, &mutation_error)) {
            if (error != nullptr) *error = mutation_error;
            return false;
        }
        for (const auto& root : prepared.roots) {
            if (!runtime.add_root(root.path, &mutation_error)) {
                if (error != nullptr) *error = mutation_error;
                return false;
            }
        }
        return true;
    }

    bool rollback_roots(const std::vector<std::filesystem::path>& previous_roots,
                        const std::string& primary_error,
                        std::string* error) {
        std::vector<ResolvedRoot> previous_resolved;
        previous_resolved.reserve(previous_roots.size());
        for (const auto& previous_root : previous_roots) {
            previous_resolved.push_back(resolve_persisted_root(previous_root));
        }

        PreparedRootSet previous_prepared;
        std::string rollback_error;
        bool restored = prepare_root_set(std::move(previous_resolved), previous_prepared, &rollback_error);
        if (restored) restored = apply_prepared_root_set(previous_prepared, &rollback_error);

        if (error != nullptr) {
            *error = primary_error.empty() ? "Search-root mutation failed" : primary_error;
            if (!restored) {
                *error += "; policy-preserving rollback failed";
                if (!rollback_error.empty()) *error += ": " + rollback_error;
            }
        }
        request_maintenance();
        return false;
    }

    bool seed_root_set(const PreparedRootSet& prepared,
                       const std::vector<std::filesystem::path>& previous_roots,
                       std::string* error) {
        std::string mutation_error;
        if (!apply_prepared_root_set(prepared, &mutation_error)) {
            return rollback_roots(previous_roots, mutation_error, error);
        }
        request_maintenance();
        return true;
    }

    void request_maintenance() noexcept {
        if (!maintenance_enabled) return;
        maintenance_pending = true;
        maintenance_fixed_ready = false;
        maintenance_fixed_index = 0;
        maintenance_fixed_exclusions.clear();
        noise_term_index = 0;
        for (auto& seen_prefixes : maintenance_seen_prefixes) seen_prefixes.clear();
    }

    bool prepare_fixed_maintenance() {
        std::string roots_error;
        const auto root_values = runtime.roots(&roots_error);
        if (!roots_error.empty()) return false;

        PreparedRootSet prepared;
        if (!prepare_root_set(resolve_persisted_roots(root_values), prepared, nullptr)) return false;
        maintenance_fixed_exclusions = std::move(prepared.exclusions);
        maintenance_fixed_index = 0;
        maintenance_fixed_ready = true;
        return true;
    }

    void scrub_one_batch() {
        if (!maintenance_pending || !maintenance_enabled) return;
        const auto runtime_status = runtime.status();
        if (runtime_status.indexing) return;

        static constexpr std::array<std::string_view, 3> noise_terms{{
            "node_modules", ".git", "__pycache__"
        }};
        static constexpr std::size_t maintenance_mutation_limit = 16u;

        if (!maintenance_fixed_ready && !prepare_fixed_maintenance()) {
            maintenance_pending = false;
            return;
        }

        std::size_t fixed_applied = 0;
        while (maintenance_fixed_index < maintenance_fixed_exclusions.size() &&
               fixed_applied < maintenance_mutation_limit) {
            const auto& excluded = maintenance_fixed_exclusions[maintenance_fixed_index++];
            std::string ignored_error;
            (void)runtime.set_indexed(path_to_utf8(excluded), false, &ignored_error);
            ++fixed_applied;
        }
        if (maintenance_fixed_index < maintenance_fixed_exclusions.size()) return;

        if (noise_term_index >= noise_terms.size()) {
            maintenance_pending = false;
            return;
        }

        const auto noise_name = noise_terms[noise_term_index];
        std::string search_error;
        const auto rows = runtime.search(noise_name, dialect, 512u, &search_error);
        if (!search_error.empty()) {
            maintenance_pending = false;
            return;
        }

        std::unordered_set<std::string> prefixes;
        for (const auto& row : rows) {
            const auto prefix = default_noise_directory_prefix(row.path, noise_name);
            if (prefix.has_value()) prefixes.insert(*prefix);
        }

        std::size_t applied = 0;
        auto& seen_prefixes = maintenance_seen_prefixes[noise_term_index];
        for (const auto& prefix : prefixes) {
            if (seen_prefixes.contains(prefix)) continue;
#if defined(_WIN32)
            const auto install = windows_such_install_root();
            if (!install.empty()) {
                const auto install_key = comparable_path_key(install);
                const auto prefix_key = comparable_path_key(path_from_utf8(prefix));
                const auto within = [](const ComparablePathKey& child, const ComparablePathKey& parent) {
                    return child == parent || (!parent.empty() && child.size() > parent.size() &&
                        child.starts_with(parent) && child[parent.size()] == L'/');
                };
                if (within(prefix_key, install_key) || within(install_key, prefix_key)) continue;
            }
#endif
            std::string ignored_error;
            if (runtime.set_indexed(prefix, false, &ignored_error)) {
                seen_prefixes.insert(prefix);
                ++applied;
                if (applied >= maintenance_mutation_limit) break;
            }
        }

        // Keep the same term only while the bounded query produces a prefix we
        // have not already handled. Some runtimes may continue returning rows
        // below a just-excluded subtree until their index view catches up; the
        // seen-prefix set guarantees forward progress.
        if (rows.empty() || applied == 0u) ++noise_term_index;
        if (noise_term_index >= noise_terms.size()) maintenance_pending = false;
    }
};

IndexRootService::IndexRootService(
    RuntimeClient& runtime,
    such::ui::PlatformDialect dialect,
    bool deferred_policy_maintenance)
    : impl_(std::make_unique<Impl>(runtime, dialect, deferred_policy_maintenance)) {}

IndexRootService::~IndexRootService() = default;

bool IndexRootService::add_root(const std::filesystem::path& root, std::string* error) {
    std::vector<ResolvedRoot> current_roots;
    if (!impl_->current_resolved_roots(current_roots, error)) return false;

    std::vector<std::filesystem::path> previous_roots;
    previous_roots.reserve(current_roots.size());
    for (const auto& value : current_roots) previous_roots.push_back(value.path);

    const auto requested = resolve_root_request(root);
    const auto requested_key = comparable_path_key(requested.path);
    const bool already_present = std::any_of(current_roots.begin(), current_roots.end(), [&](const auto& value) {
        return comparable_path_key(value.path) == requested_key;
    });
    current_roots.push_back(requested);
    PreparedRootSet prepared;
    if (!prepare_root_set(current_roots, prepared, error)) return false;

    // An explicit AppData add is an exception to an otherwise sticky parent
    // exclusion. Re-seed the full root set even if that root already exists so
    // stale parent exclusions are actually cleared.
    if (requested.override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData) {
        return impl_->seed_root_set(prepared, previous_roots, error);
    }

    if (already_present) {
        if (!apply_exclusions(impl_->runtime, prepared.exclusions, error)) return false;
        impl_->request_maintenance();
        return true;
    }

    // add_root preserves existing exclusion state. Compile the policy against
    // the complete root set so a pre-existing explicit AppData root remains a
    // global exception when a broader root is added later.
    if (!apply_exclusions(impl_->runtime, prepared.exclusions, error)) return false;
    if (!impl_->runtime.add_root(requested.path, error)) return false;
    impl_->request_maintenance();
    return true;
}

bool IndexRootService::replace_roots(
    const std::vector<std::filesystem::path>& roots,
    std::string* error) {
    PreparedRootSet prepared;
    if (!impl_->prepare_requested_roots(roots, prepared, error)) return false;

    std::string roots_error;
    const auto previous_values = impl_->runtime.roots(&roots_error);
    if (!roots_error.empty()) {
        if (error != nullptr) *error = roots_error;
        return false;
    }
    std::vector<std::filesystem::path> previous_roots;
    previous_roots.reserve(previous_values.size());
    for (const auto& value : previous_values) previous_roots.push_back(path_from_utf8(value));

    return impl_->seed_root_set(prepared, previous_roots, error);
}

bool IndexRootService::remove_root(const std::filesystem::path& root, std::string* error) {
    const auto requested = resolve_root_request(root);
    const auto requested_key = comparable_path_key(requested.path);

    std::vector<ResolvedRoot> current_roots;
    if (!impl_->current_resolved_roots(current_roots, error)) return false;

    std::vector<std::filesystem::path> previous_roots;
    previous_roots.reserve(current_roots.size());
    for (const auto& value : current_roots) previous_roots.push_back(value.path);

    bool removed_explicit_appdata = false;
    current_roots.erase(
        std::remove_if(current_roots.begin(), current_roots.end(), [&](const auto& value) {
            if (comparable_path_key(value.path) != requested_key) return false;
            removed_explicit_appdata =
                removed_explicit_appdata ||
                value.override_mode == IndexPolicyOverride::ExplicitCurrentUserAppData;
            return true;
        }),
        current_roots.end());

    PreparedRootSet prepared;
    if (!prepare_root_set(std::move(current_roots), prepared, error)) return false;

    // Explicit AppData is a global exception to parent-dominant exclusions.
    // Removing it must reseed the complete remaining root set; a raw remove_root
    // would leave the old exception state sticky in the private runtime.
    if (removed_explicit_appdata) {
        return impl_->seed_root_set(prepared, previous_roots, error);
    }

    if (!impl_->runtime.remove_root(requested.path, error)) return false;
    if (!apply_exclusions(impl_->runtime, prepared.exclusions, error)) return false;
    impl_->request_maintenance();
    return true;
}

bool IndexRootService::reindex_async(std::string* error) {
    std::string roots_error;
    const auto root_values = impl_->runtime.roots(&roots_error);
    if (!roots_error.empty()) {
        if (error != nullptr) *error = roots_error;
        return false;
    }
    if (root_values.empty()) {
        impl_->runtime.reindex_async();
        impl_->request_maintenance();
        return true;
    }

    PreparedRootSet prepared;
    if (!prepare_root_set(resolve_persisted_roots(root_values), prepared, error)) return false;
    std::vector<std::filesystem::path> previous_roots;
    previous_roots.reserve(root_values.size());
    for (const auto& value : root_values) previous_roots.push_back(path_from_utf8(value));
    return impl_->seed_root_set(prepared, previous_roots, error);
}

void IndexRootService::refresh_policy_async() {
    // Startup remains non-destructive: tighten current policy in bounded idle
    // batches, but do not rebuild roots or perform migration in ensure_ready().
    impl_->request_maintenance();
}

void IndexRootService::poll_policy_maintenance() {
    impl_->scrub_one_batch();
}

bool IndexRootService::policy_maintenance_active() const noexcept {
    return impl_->maintenance_pending;
}

} // namespace such::runtime

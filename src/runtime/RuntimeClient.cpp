#include <such/runtime/RuntimeClient.h>
#include <such/runtime/RuntimeABI.h>
#include <such/runtime/IndexPathPolicy.h>

#include <cstdlib>
#include <array>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <unordered_set>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <dlfcn.h>
#include <mach-o/dyld.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#endif

namespace such::runtime {
namespace {

std::optional<std::string> env_value(const char* name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) return std::nullopt;
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    if (const char* value = std::getenv(name)) return std::string(value);
    return std::nullopt;
#endif
}

std::string path_to_utf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    return path.string();
#endif
}

struct RootRequest {
    std::filesystem::path path;
    IndexPolicyOverride override_mode = IndexPolicyOverride::None;
};

RootRequest resolve_root_request(const std::filesystem::path& requested) {
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

std::filesystem::path executable_directory() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0 || static_cast<std::size_t>(len) >= buffer.size()) return {};
    buffer.resize(static_cast<std::size_t>(len));
    return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    if (size == 0) return {};
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    return std::filesystem::path(buffer.c_str()).parent_path();
#else
    std::string buffer(PATH_MAX, '\0');
    const ssize_t len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1u);
    if (len <= 0) return {};
    buffer.resize(static_cast<std::size_t>(len));
    return std::filesystem::path(buffer).parent_path();
#endif
}

#if defined(_WIN32)
using LibraryHandle = HMODULE;
using LibrarySymbol = FARPROC;
LibraryHandle open_library(const std::filesystem::path& path) { return LoadLibraryW(path.c_str()); }
void close_library(LibraryHandle library) { if (library != nullptr) FreeLibrary(library); }
LibrarySymbol load_symbol(LibraryHandle library, const char* name) { return GetProcAddress(library, name); }
constexpr const wchar_t* kRuntimeFileName = L"SuchRuntimePrivate.dll";
#elif defined(__APPLE__)
using LibraryHandle = void*;
using LibrarySymbol = void*;
LibraryHandle open_library(const std::filesystem::path& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void close_library(LibraryHandle library) { if (library != nullptr) dlclose(library); }
LibrarySymbol load_symbol(LibraryHandle library, const char* name) { return dlsym(library, name); }
constexpr const char* kRuntimeFileName = "libSuchRuntimePrivate.dylib";
#else
using LibraryHandle = void*;
using LibrarySymbol = void*;
LibraryHandle open_library(const std::filesystem::path& path) { return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL); }
void close_library(LibraryHandle library) { if (library != nullptr) dlclose(library); }
LibrarySymbol load_symbol(LibraryHandle library, const char* name) { return dlsym(library, name); }
constexpr const char* kRuntimeFileName = "libSuchRuntimePrivate.so";
#endif

std::filesystem::path default_library_path() {
    if (const auto explicit_path = env_value("SUCH_RUNTIME_LIBRARY")) return std::filesystem::path(*explicit_path);
    const auto directory = executable_directory();
    if (directory.empty()) return {};
    return directory / kRuntimeFileName;
}

int append_string(void* user, const char* value) {
    if (user == nullptr || value == nullptr) return 0;
    static_cast<std::vector<std::string>*>(user)->emplace_back(value);
    return 1;
}

int append_result(void* user, const such_runtime_result_v1* value) {
    if (user == nullptr || value == nullptr) return 0;
    auto* out = static_cast<std::vector<such::ui::ResultItem>*>(user);
    such::ui::ResultItem item;
    item.file_id = value->file_id;
    item.filename = value->filename != nullptr ? value->filename : "";
    item.path = value->path != nullptr ? value->path : "";
    item.extension = value->extension != nullptr ? value->extension : "";
    item.pinned = value->pinned != 0;
    item.indexed = value->indexed != 0;
    item.authorized = value->authorized != 0;
    item.modified_unix_seconds = value->modified_unix_seconds;
    item.size_bytes = value->size_bytes;
    out->push_back(std::move(item));
    return 1;
}


bool path_component_equal(std::string_view lhs, std::string_view rhs) {
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
}

std::optional<std::string> directory_prefix_for_component(std::string_view path, std::string_view target) {
    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && (path[start] == '/' || path[start] == '\\')) ++start;
        if (start >= path.size()) break;
        std::size_t end = start;
        while (end < path.size() && path[end] != '/' && path[end] != '\\') ++end;
        if (path_component_equal(path.substr(start, end - start), target)) {
            return std::string(path.substr(0, end));
        }
        start = end + 1;
    }
    return std::nullopt;
}

struct NoiseScrubContext {
    std::string_view target;
    std::unordered_set<std::string> seen;
    std::vector<std::string> directories;
};

int collect_noise_directory(void* user, const such_runtime_result_v1* value) {
    if (user == nullptr || value == nullptr || value->path == nullptr) return 0;
    auto* context = static_cast<NoiseScrubContext*>(user);
    const auto prefix = directory_prefix_for_component(value->path, context->target);
    if (!prefix.has_value()) return 1;
    std::string key = *prefix;
#if defined(_WIN32)
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    if (context->seen.insert(std::move(key)).second) context->directories.push_back(*prefix);
    return 1;
}

int append_content_result(void* user, const such_runtime_content_result_v1* value) {
    if (user == nullptr || value == nullptr) return 0;
    auto* out = static_cast<std::vector<such::ui::ResultItem>*>(user);
    such::ui::ResultItem item;
    item.file_id = value->file_id;
    item.filename = value->filename != nullptr ? value->filename : "";
    item.path = value->path != nullptr ? value->path : "";
    item.extension = value->extension != nullptr ? value->extension : "";
    item.content_match = true;
    item.locator_kind = static_cast<such::ui::ContentLocatorKind>(value->locator_kind);
    item.page_number = value->page_number;
    item.line_number = value->line_number;
    item.slide_number = value->slide_number;
    item.sheet_number = value->sheet_number;
    item.byte_offset = value->byte_offset;
    item.logical_name = value->logical_name != nullptr ? value->logical_name : "";
    item.snippet = value->snippet != nullptr ? value->snippet : "";
    item.content_score = value->score;
    out->push_back(std::move(item));
    return 1;
}

} // namespace

struct RuntimeClient::Impl {
    using AbiVersionFn = std::uint32_t (*)();
    using CreateFn = such_runtime_handle_v1 (*)(const char*);
    using DestroyFn = void (*)(such_runtime_handle_v1);
    using LoadFn = int (*)(such_runtime_handle_v1);
    using LastErrorFn = const char* (*)(such_runtime_handle_v1);
    using PathMutationFn = int (*)(such_runtime_handle_v1, const char*);
    using ReplaceRootsFn = int (*)(such_runtime_handle_v1, const char* const*, std::size_t);
    using ListStringsFn = int (*)(such_runtime_handle_v1, such_runtime_string_callback_v1, void*);
    using VoidHandleFn = void (*)(such_runtime_handle_v1);
    using SearchFn = int (*)(such_runtime_handle_v1, const char*, std::uint32_t, std::size_t, such_runtime_result_callback_v1, void*);
    using ContentSearchFn = int (*)(such_runtime_handle_v1, const such_runtime_content_search_request_v1*, such_runtime_content_result_callback_v1, void*);
    using BoolMutationFn = int (*)(such_runtime_handle_v1, const char*, int);
    using StatusFn = int (*)(such_runtime_handle_v1, such_runtime_status_v1*);
    using EngineStatsFn = int (*)(such_runtime_handle_v1, such_runtime_engine_stats_v1*);
    using GenerationFn = std::uint64_t (*)(such_runtime_handle_v1);

    explicit Impl(std::filesystem::path state) : state_dir(std::move(state)) {}
    ~Impl() { reset_loaded_state(); }

    void reset_loaded_state() noexcept {
        if (runtime != nullptr && destroy != nullptr) destroy(runtime);
        runtime = nullptr;
        ready = false;
        close_library(library);
        library = nullptr;
        abi_version = nullptr;
        create = nullptr;
        destroy = nullptr;
        load_fn = nullptr;
        last_error_fn = nullptr;
        add_root_fn = nullptr;
        remove_root_fn = nullptr;
        replace_roots_fn = nullptr;
        list_roots_fn = nullptr;
        list_extensions_fn = nullptr;
        reindex_async_fn = nullptr;
        wait_for_idle_fn = nullptr;
        search_fn = nullptr;
        search_content_fn = nullptr;
        set_pinned_fn = nullptr;
        set_indexed_fn = nullptr;
        status_fn = nullptr;
        engine_stats_fn = nullptr;
        generation_fn = nullptr;
    }

    bool fail_and_reset(std::string message) {
        reset_loaded_state();
        error = std::move(message);
        return false;
    }

    template <class Fn>
    bool bind(Fn& out, const char* name) {
        const LibrarySymbol symbol = load_symbol(library, name);
        if (symbol == nullptr) {
            error = std::string("missing runtime ABI symbol: ") + name;
            return false;
        }
        static_assert(sizeof(Fn) == sizeof(LibrarySymbol), "runtime function pointer size mismatch");
        std::memcpy(&out, &symbol, sizeof(out));
        return true;
    }

    template <class Fn>
    void bind_optional(Fn& out, const char* name) {
        const LibrarySymbol symbol = load_symbol(library, name);
        if (symbol == nullptr) {
            out = nullptr;
            return;
        }
        static_assert(sizeof(Fn) == sizeof(LibrarySymbol), "runtime function pointer size mismatch");
        std::memcpy(&out, &symbol, sizeof(out));
    }

    bool ensure_loaded() {
        if (runtime != nullptr) return true;
        const auto library_path = default_library_path();
        if (library_path.empty()) {
            error = "Such could not resolve its executable directory for runtime discovery";
            return false;
        }
        library = open_library(library_path);
        if (library == nullptr) {
            error = "Such private runtime is not installed. Expected: " + path_to_utf8(library_path);
            return false;
        }
        if (!bind(abi_version, "such_runtime_abi_version") ||
            !bind(create, "such_runtime_create_v1") ||
            !bind(destroy, "such_runtime_destroy_v1") ||
            !bind(load_fn, "such_runtime_load_v1") ||
            !bind(last_error_fn, "such_runtime_last_error_v1") ||
            !bind(add_root_fn, "such_runtime_add_root_v1") ||
            !bind(remove_root_fn, "such_runtime_remove_root_v1") ||
            !bind(replace_roots_fn, "such_runtime_replace_roots_v1") ||
            !bind(list_roots_fn, "such_runtime_list_roots_v1") ||
            !bind(list_extensions_fn, "such_runtime_list_extensions_v1") ||
            !bind(reindex_async_fn, "such_runtime_reindex_async_v1") ||
            !bind(wait_for_idle_fn, "such_runtime_wait_for_idle_v1") ||
            !bind(search_fn, "such_runtime_search_v1") ||
            !bind(set_pinned_fn, "such_runtime_set_pinned_v1") ||
            !bind(set_indexed_fn, "such_runtime_set_indexed_v1") ||
            !bind(status_fn, "such_runtime_status_v1_fn") ||
            !bind(engine_stats_fn, "such_runtime_engine_stats_v1_fn") ||
            !bind(generation_fn, "such_runtime_generation_v1")) {
            const std::string bind_error = error;
            return fail_and_reset(bind_error);
        }
        // Content search is an ABI-v1 extension. Its absence must not prevent
        // older runtimes from serving ordinary file searches.
        bind_optional(search_content_fn, "such_runtime_search_content_v1");
        if (abi_version() != SUCH_RUNTIME_ABI_VERSION) {
            return fail_and_reset("Such private runtime ABI version mismatch");
        }
        const std::string state_utf8 = state_dir.empty() ? std::string{} : path_to_utf8(state_dir);
        runtime = create(state_utf8.empty() ? nullptr : state_utf8.c_str());
        if (runtime == nullptr) {
            return fail_and_reset("Such private runtime could not be created");
        }
        return true;
    }

    bool ensure_ready() {
        if (ready) return true;
        if (!ensure_loaded()) return false;
        if (load_fn(runtime) == 0) {
            const std::string load_error = abi_error();
            return fail_and_reset(load_error.empty() ? "Such private runtime could not be loaded" : load_error);
        }
        ready = true;
        error.clear();
        return true;
    }

    void apply_existing_index_policy() {
        if (runtime == nullptr || set_indexed_fn == nullptr) return;

        // Fast fixed-path migration for existing roots. This immediately hides
        // old AppData/Windows rows after an upgrade without requiring a full
        // reindex. New root mutations still run the full pre-crawl discovery.
        std::vector<std::string> root_values;
        if (list_roots_fn != nullptr && list_roots_fn(runtime, append_string, &root_values) != 0) {
            for (const auto& value : root_values) {
                const auto root_path = std::filesystem::path(value);
                const auto mode = is_current_user_manual_appdata_root(root_path)
                    ? IndexPolicyOverride::ExplicitCurrentUserAppData
                    : IndexPolicyOverride::None;
                const auto plan = plan_default_index_policy(root_path, false, mode);
                if (!plan.root_allowed) {
                    (void)set_indexed_fn(runtime, value.c_str(), 0);
                    continue;
                }
                for (const auto& excluded : plan.exclusions) {
                    const auto path = path_to_utf8(excluded);
                    (void)set_indexed_fn(runtime, path.c_str(), 0);
                }
            }
        }

        // Existing catalogs from older builds may already contain dependency
        // trees. Find their concrete directory prefixes through the file index,
        // then convert them to persistent subtree exclusions. Subsequent starts
        // see no matching rows, so this migration becomes three cheap queries.
        const std::array<std::string_view, 3> noise{{"node_modules", ".git", "__pycache__"}};
#if defined(_WIN32)
        constexpr std::uint32_t raw_dialect = 0u;
#else
        constexpr std::uint32_t raw_dialect = 1u;
#endif
        for (const auto name : noise) {
            NoiseScrubContext context{name, {}, {}};
            if (search_fn(runtime, std::string(name).c_str(), raw_dialect, 0, collect_noise_directory, &context) == 0) continue;
            for (const auto& directory : context.directories) {
                (void)set_indexed_fn(runtime, directory.c_str(), 0);
            }
        }
    }

    bool apply_index_policy(const std::filesystem::path& root,
                            bool discover_noise_directories,
                            IndexPolicyOverride override_mode,
                            std::string* out_error) {
        const auto plan = plan_default_index_policy(root, discover_noise_directories, override_mode);
        if (!plan.root_allowed) {
            error = plan.rejection_reason.empty() ? "Search root is blocked by the default index policy" : plan.rejection_reason;
            if (out_error != nullptr) *out_error = error;
            return false;
        }
        for (const auto& excluded : plan.exclusions) {
            const auto value = path_to_utf8(excluded);
            if (set_indexed_fn(runtime, value.c_str(), 0) == 0) {
                const auto abi = abi_error();
                error = abi.empty() ? "Such could not apply a default index exclusion: " + value : abi;
                if (out_error != nullptr) *out_error = error;
                return false;
            }
        }
        return true;
    }

    std::string abi_error() const {
        if (runtime != nullptr && last_error_fn != nullptr) {
            if (const char* value = last_error_fn(runtime); value != nullptr && *value != '\0') return value;
        }
        return {};
    }

    std::string runtime_error() const {
        if (!error.empty()) return error;
        return abi_error();
    }

    std::filesystem::path state_dir;
    mutable std::string error;
    LibraryHandle library = nullptr;
    such_runtime_handle_v1 runtime = nullptr;
    bool ready = false;
    AbiVersionFn abi_version = nullptr;
    CreateFn create = nullptr;
    DestroyFn destroy = nullptr;
    LoadFn load_fn = nullptr;
    LastErrorFn last_error_fn = nullptr;
    PathMutationFn add_root_fn = nullptr;
    PathMutationFn remove_root_fn = nullptr;
    ReplaceRootsFn replace_roots_fn = nullptr;
    ListStringsFn list_roots_fn = nullptr;
    ListStringsFn list_extensions_fn = nullptr;
    VoidHandleFn reindex_async_fn = nullptr;
    VoidHandleFn wait_for_idle_fn = nullptr;
    SearchFn search_fn = nullptr;
    ContentSearchFn search_content_fn = nullptr;
    BoolMutationFn set_pinned_fn = nullptr;
    BoolMutationFn set_indexed_fn = nullptr;
    StatusFn status_fn = nullptr;
    EngineStatsFn engine_stats_fn = nullptr;
    GenerationFn generation_fn = nullptr;
};

RuntimeClient::RuntimeClient(std::filesystem::path state_directory)
    : impl_(std::make_unique<Impl>(std::move(state_directory))) {}
RuntimeClient::~RuntimeClient() = default;

bool RuntimeClient::load(std::string* error) {
    if (!impl_->ensure_ready()) {
        if (error != nullptr) *error = impl_->runtime_error();
        return false;
    }
    return true;
}

bool RuntimeClient::available() const noexcept { return impl_->ready; }
std::string RuntimeClient::last_error() const { return impl_->runtime_error(); }

bool RuntimeClient::add_root(const std::filesystem::path& root, std::string* error) {
    if (!impl_->ensure_ready()) { if (error != nullptr) *error = impl_->runtime_error(); return false; }
    const auto request = resolve_root_request(root);
    if (!impl_->apply_index_policy(request.path, true, request.override_mode, error)) return false;
    const auto path = path_to_utf8(request.path);
    const bool ok = impl_->add_root_fn(impl_->runtime, path.c_str()) != 0;
    if (ok) impl_->error.clear();
    else if (error != nullptr) *error = impl_->runtime_error();
    return ok;
}

bool RuntimeClient::remove_root(const std::filesystem::path& root, std::string* error) {
    if (!impl_->ensure_ready()) { if (error != nullptr) *error = impl_->runtime_error(); return false; }
    const auto path = path_to_utf8(root);
    const bool ok = impl_->remove_root_fn(impl_->runtime, path.c_str()) != 0;
    if (ok) impl_->error.clear();
    else if (error != nullptr) *error = impl_->runtime_error();
    return ok;
}

bool RuntimeClient::replace_roots(const std::vector<std::filesystem::path>& roots_value, std::string* error) {
    if (!impl_->ensure_ready()) { if (error != nullptr) *error = impl_->runtime_error(); return false; }
    std::vector<RootRequest> requests;
    requests.reserve(roots_value.size());
    for (const auto& root : roots_value) {
        auto request = resolve_root_request(root);
        if (!impl_->apply_index_policy(request.path, true, request.override_mode, error)) return false;
        requests.push_back(std::move(request));
    }
    std::vector<std::string> storage;
    std::vector<const char*> pointers;
    storage.reserve(requests.size());
    pointers.reserve(requests.size());
    for (const auto& request : requests) storage.push_back(path_to_utf8(request.path));
    for (const auto& value : storage) pointers.push_back(value.c_str());
    const bool ok = impl_->replace_roots_fn(impl_->runtime, pointers.data(), pointers.size()) != 0;
    if (ok) impl_->error.clear();
    else if (error != nullptr) *error = impl_->runtime_error();
    return ok;
}

std::vector<std::string> RuntimeClient::roots(std::string* error) const {
    std::vector<std::string> out;
    if (!impl_->ready || impl_->runtime == nullptr || impl_->list_roots_fn == nullptr) {
        if (error != nullptr) *error = impl_->runtime_error();
        return out;
    }
    if (impl_->list_roots_fn(impl_->runtime, append_string, &out) == 0) {
        const std::string abi = impl_->abi_error();
        impl_->error = abi.empty() ? "Such private runtime could not list search roots" : abi;
        if (error != nullptr) *error = impl_->error;
        out.clear();
        return out;
    }
    impl_->error.clear();
    return out;
}

void RuntimeClient::reindex_async() {
    if (!impl_->ready || impl_->runtime == nullptr || impl_->reindex_async_fn == nullptr) return;
    std::vector<std::string> root_values;
    if (impl_->list_roots_fn != nullptr && impl_->list_roots_fn(impl_->runtime, append_string, &root_values) != 0) {
        for (const auto& value : root_values) {
            const auto root_path = std::filesystem::path(value);
            const auto mode = is_current_user_manual_appdata_root(root_path)
                ? IndexPolicyOverride::ExplicitCurrentUserAppData
                : IndexPolicyOverride::None;
            std::string ignored_error;
            (void)impl_->apply_index_policy(root_path, true, mode, &ignored_error);
        }
    }
    impl_->reindex_async_fn(impl_->runtime);
}
void RuntimeClient::wait_for_idle() {
    if (impl_->ready && impl_->runtime != nullptr && impl_->wait_for_idle_fn != nullptr) impl_->wait_for_idle_fn(impl_->runtime);
}

std::vector<such::ui::ResultItem> RuntimeClient::search(
    std::string_view query,
    such::ui::PlatformDialect dialect,
    std::size_t max_results,
    std::string* error) const {
    if (!impl_->ready || impl_->runtime == nullptr || impl_->search_fn == nullptr) {
        if (error != nullptr) *error = impl_->runtime_error();
        return {};
    }

    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    const auto observed = observed_extensions();
    const auto raw_dialect = dialect == such::ui::PlatformDialect::Windows ? 0u : 1u;

    auto lower_copy = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };

    auto search_files = [&](std::string_view file_query, std::size_t result_limit, std::string* local_error) {
        std::vector<such::ui::ResultItem> raw;
        const auto filter = such::ui::parse_search_query(file_query, dialect, now, observed);
        const std::string runtime_query = such::ui::compile_runtime_query(filter, dialect);

        // Filtering can discard rows after runtime ranking. Ask for a wider
        // candidate set when a finite cap was requested.
        const std::size_t candidate_limit = result_limit == 0
            ? 0
            : std::max<std::size_t>(result_limit,
                  std::min<std::size_t>(result_limit > 512u ? 4096u : result_limit * 8u, 4096u));
        if (impl_->search_fn(impl_->runtime, runtime_query.c_str(), raw_dialect, candidate_limit, append_result, &raw) == 0) {
            const std::string abi = impl_->abi_error();
            impl_->error = abi.empty() ? "Such private runtime search failed" : abi;
            if (local_error != nullptr) *local_error = impl_->error;
            return std::vector<such::ui::ResultItem>{};
        }

        std::unordered_set<std::string> wanted_extensions;
        wanted_extensions.reserve(filter.extensions.size());
        for (auto ext : filter.extensions) {
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
            wanted_extensions.insert(std::move(ext));
        }

        std::vector<std::string> detail_terms;
        detail_terms.reserve(filter.detail_terms.size());
        for (const auto& term : filter.detail_terms) detail_terms.push_back(lower_copy(term));

        std::vector<such::ui::ResultItem> out;
        out.reserve(raw.size());
        for (auto& item : raw) {
            if (filter.pinned_only && !item.pinned) continue;
            if (!wanted_extensions.empty()) {
                std::string ext = item.extension;
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
                if (!wanted_extensions.contains(ext)) continue;
            }
            if (filter.modified.after_unix_seconds.has_value() &&
                item.modified_unix_seconds < *filter.modified.after_unix_seconds) continue;
            if (filter.modified.before_unix_seconds.has_value() &&
                item.modified_unix_seconds >= *filter.modified.before_unix_seconds) continue;
            if (!detail_terms.empty()) {
                const std::string haystack = lower_copy(item.filename + "\n" + item.path);
                bool detail_match = true;
                for (const auto& term : detail_terms) {
                    if (!term.empty() && haystack.find(term) == std::string::npos) {
                        detail_match = false;
                        break;
                    }
                }
                if (!detail_match) continue;
            }
            out.push_back(std::move(item));
            if (result_limit != 0 && out.size() >= result_limit) break;
        }
        impl_->error.clear();
        if (local_error != nullptr) local_error->clear();
        return out;
    };

    auto search_content = [&](const such::ui::SearchFilter& filter,
                              std::vector<std::uint64_t> candidate_ids,
                              bool candidate_set_is_restrictive,
                              std::string* local_error) {
        if (impl_->search_content_fn == nullptr) {
            impl_->error = "Such /inside requires a content-search capable private runtime (v0.7.0 or newer)";
            if (local_error != nullptr) *local_error = impl_->error;
            return std::vector<such::ui::ResultItem>{};
        }
        if (filter.text.empty()) {
            impl_->error.clear();
            if (local_error != nullptr) local_error->clear();
            return std::vector<such::ui::ResultItem>{};
        }

        // Direct content queries can still carry file-level constraints such as
        // /docx, /pin or dates. Resolve those constraints into a candidate set
        // before entering the content engine rather than post-filtering hits.
        if (!candidate_set_is_restrictive) {
            such::ui::SearchFilter candidate_filter = filter;
            candidate_filter.scope = such::ui::SearchScope::File;
            candidate_filter.text.clear();
            candidate_filter.detail_terms.clear();
            const bool has_file_constraints = candidate_filter.pinned_only ||
                !candidate_filter.extensions.empty() ||
                candidate_filter.modified.after_unix_seconds.has_value() ||
                candidate_filter.modified.before_unix_seconds.has_value();
            if (has_file_constraints) {
                const std::string candidate_query = such::ui::compile_runtime_query(candidate_filter, dialect);
                std::string candidate_error;
                const auto candidates = search_files(candidate_query, 0, &candidate_error);
                if (!candidate_error.empty()) {
                    if (local_error != nullptr) *local_error = candidate_error;
                    return std::vector<such::ui::ResultItem>{};
                }
                if (candidates.empty()) {
                    impl_->error.clear();
                    if (local_error != nullptr) local_error->clear();
                    return std::vector<such::ui::ResultItem>{};
                }
                candidate_ids.reserve(candidates.size());
                for (const auto& item : candidates) candidate_ids.push_back(item.file_id);
                candidate_set_is_restrictive = true;
            }
        }

        such_runtime_content_search_request_v1 request{};
        request.query_utf8 = filter.text.c_str();
        request.candidate_file_ids = candidate_set_is_restrictive ? candidate_ids.data() : nullptr;
        request.candidate_file_count = candidate_set_is_restrictive ? candidate_ids.size() : 0;
        request.max_results = max_results;
        request.flags = 0;

        std::vector<such::ui::ResultItem> out;
        if (impl_->search_content_fn(impl_->runtime, &request, append_content_result, &out) == 0) {
            const std::string abi = impl_->abi_error();
            impl_->error = abi.empty() ? "Such private runtime content search failed" : abi;
            if (local_error != nullptr) *local_error = impl_->error;
            return std::vector<such::ui::ResultItem>{};
        }
        impl_->error.clear();
        if (local_error != nullptr) local_error->clear();
        return out;
    };

    // Drill Search becomes a real staged plan when the final branch is /inside:
    // preceding file branches form a ResultSet, then the content runtime searches
    // only those file IDs. Content scope in the middle of a tree is deliberately
    // rejected until a subsequent content->file stage has explicit semantics.
    const auto detail = such::ui::parse_detail_search(query);
    if (detail.active) {
        std::vector<std::string> stages;
        stages.reserve(detail.refinements.size() + 1u);
        stages.push_back(detail.primary);
        stages.insert(stages.end(), detail.refinements.begin(), detail.refinements.end());

        std::size_t content_stage = stages.size();
        for (std::size_t i = 0; i < stages.size(); ++i) {
            const auto stage_filter = such::ui::parse_search_query(stages[i], dialect, now, observed);
            if (stage_filter.scope == such::ui::SearchScope::Content) {
                content_stage = i;
                break;
            }
        }
        if (content_stage != stages.size()) {
            if (content_stage + 1u != stages.size()) {
                impl_->error = "/inside must be the final Drill Search stage";
                if (error != nullptr) *error = impl_->error;
                return {};
            }

            std::vector<std::uint64_t> candidate_ids;
            bool restricted = false;
            if (content_stage > 0u) {
                std::string candidate_query = stages.front();
                for (std::size_t i = 1; i < content_stage; ++i) {
                    candidate_query += " /; ";
                    candidate_query += stages[i];
                }
                std::string candidate_error;
                const auto candidates = search_files(candidate_query, 0, &candidate_error);
                if (!candidate_error.empty()) {
                    if (error != nullptr) *error = candidate_error;
                    return {};
                }
                if (candidates.empty()) {
                    impl_->error.clear();
                    if (error != nullptr) error->clear();
                    return {};
                }
                candidate_ids.reserve(candidates.size());
                for (const auto& item : candidates) candidate_ids.push_back(item.file_id);
                restricted = true;
            }
            const auto content_filter = such::ui::parse_search_query(stages[content_stage], dialect, now, observed);
            return search_content(content_filter, std::move(candidate_ids), restricted, error);
        }
    }

    const auto filter = such::ui::parse_search_query(query, dialect, now, observed);
    if (filter.scope == such::ui::SearchScope::Content) {
        return search_content(filter, {}, false, error);
    }
    return search_files(query, max_results, error);
}

std::vector<std::string> RuntimeClient::observed_extensions() const {
    std::vector<std::string> out;
    if (impl_->ready && impl_->runtime != nullptr && impl_->list_extensions_fn != nullptr) {
        (void)impl_->list_extensions_fn(impl_->runtime, append_string, &out);
    }
    return out;
}

bool RuntimeClient::set_pinned(std::string_view utf8_path, bool pinned, std::string* error) {
    if (!impl_->ensure_ready()) { if (error != nullptr) *error = impl_->runtime_error(); return false; }
    const std::string path(utf8_path);
    const bool ok = impl_->set_pinned_fn(impl_->runtime, path.c_str(), pinned ? 1 : 0) != 0;
    if (ok) impl_->error.clear();
    else if (error != nullptr) *error = impl_->runtime_error();
    return ok;
}

bool RuntimeClient::set_indexed(std::string_view utf8_path, bool indexed, std::string* error) {
    if (!impl_->ensure_ready()) { if (error != nullptr) *error = impl_->runtime_error(); return false; }
    const std::string path(utf8_path);
    const bool ok = impl_->set_indexed_fn(impl_->runtime, path.c_str(), indexed ? 1 : 0) != 0;
    if (ok) impl_->error.clear();
    else if (error != nullptr) *error = impl_->runtime_error();
    return ok;
}

RuntimeStatus RuntimeClient::status() const {
    RuntimeStatus out;
    std::string roots_error;
    out.roots = roots(&roots_error);
    if (!roots_error.empty()) out.last_error = roots_error;
    if (!impl_->ready || impl_->runtime == nullptr || impl_->status_fn == nullptr) {
        out.last_error = impl_->runtime_error();
        return out;
    }
    such_runtime_status_v1 raw{};
    if (impl_->status_fn(impl_->runtime, &raw) == 0) {
        out.last_error = impl_->runtime_error();
        return out;
    }
    out.indexing = raw.indexing != 0;
    out.indexed_files = static_cast<std::size_t>(raw.indexed_files);
    out.generation = raw.generation;
    return out;
}

RuntimeEngineStats RuntimeClient::engine_stats() const noexcept {
    RuntimeEngineStats out;
    if (!impl_->ready || impl_->runtime == nullptr || impl_->engine_stats_fn == nullptr) return out;
    such_runtime_engine_stats_v1 raw{};
    if (impl_->engine_stats_fn(impl_->runtime, &raw) == 0) return out;
    out.rows = static_cast<std::size_t>(raw.rows);
    out.chunks = static_cast<std::size_t>(raw.chunks);
    out.candidate_chunks = static_cast<std::size_t>(raw.candidate_chunks);
    out.candidate_rows = static_cast<std::size_t>(raw.candidate_rows);
    out.pruned_by_time = static_cast<std::size_t>(raw.pruned_by_time);
    out.pruned_by_extension = static_cast<std::size_t>(raw.pruned_by_extension);
    out.pruned_by_text = static_cast<std::size_t>(raw.pruned_by_text);
    out.workers_used = static_cast<std::size_t>(raw.workers_used);
    out.parallel = raw.parallel != 0;
    out.backend_loaded = raw.backend_loaded != 0;
    out.backend_active = raw.backend_active != 0;
    out.backend_candidate_rows = static_cast<std::size_t>(raw.backend_candidate_rows);
    out.dynamic_queries = raw.dynamic_queries;
    out.dynamic_tiles = raw.dynamic_tiles;
    out.direct_compaction_queries = raw.direct_compaction_queries;
    return out;
}

std::uint64_t RuntimeClient::generation() const noexcept {
    return impl_->ready && impl_->runtime != nullptr && impl_->generation_fn != nullptr ? impl_->generation_fn(impl_->runtime) : 0u;
}

} // namespace such::runtime

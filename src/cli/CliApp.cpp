#include <such/cli/CliApp.h>

#include <such/Version.h>
#include <such/cli/ProcessControl.h>
#include <such/runtime/RuntimeClient.h>
#include <such/runtime/IndexRootService.h>
#include <such/runtime/DailyIndexRefresh.h>
#include <such/runtime/IndexSnapshot.h>
#include <such/ui/FontSettings.h>
#include <such/ui/FrontendLease.h>
#include <such/ui/IndexCommands.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace such::cli {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string join_args(int argc, char* const* argv, int first) {
    std::string out;
    for (int i = first; i < argc; ++i) {
        if (!out.empty()) out.push_back(' ');
        out += argv[i] ? argv[i] : "";
    }
    return out;
}

void print_help() {
    std::printf(
        "Such %.*s\n"
        "\n"
        "  such <query>          search\n"
        "  such root             list search roots\n"
        "  such root <folder>    use one root\n"
        "  such root + <folder>  add a root\n"
        "  such drive <folder>   choose one search folder\n"
        "  such scan             rebuild index\n"
        "  such snapshot export <file.sidx>  export reusable index state\n"
        "  such snapshot import <file.sidx>  import without full recrawl\n"
        "  such snapshot info <file.sidx>    inspect snapshot metadata\n"
        "  such status           show index status\n"
        "  such pin <file>       pin a file\n"
        "  such unpin <file>     unpin a file\n"
        "  such hide <file>      exclude a file\n"
        "  such show <file>      include a file\n"
        "  such font [name]      list/set font\n"
        "  such stop [--force]   stop Such processes\n"
        "  such version          print version\n"
        "\n"
        "Search box: /drive <folder> on Windows, //drive <folder> on Linux\n"
        "Aliases: roots/use/add, find/search, drive, index, reindex, shutdown\n",
        static_cast<int>(such::version::kVersion.size()), such::version::kVersion.data());
}

int print_runtime_error(const char* label, const std::string& error, int code) {
    std::fprintf(stderr, "%s: %s\n", label, error.c_str());
    return code;
}

bool is_verb(std::string_view value, std::initializer_list<std::string_view> names) {
    for (const auto name : names) if (value == name) return true;
    return false;
}

bool stdin_is_terminal() noexcept {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

std::vector<std::string> split_interactive_command(std::string_view line) {
    std::vector<std::string> arguments;
    std::string current;
    char quote = '\0';
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote != '\0') {
            if (c == quote) { quote = '\0'; continue; }
            if (c == '\\' && i + 1 < line.size() && line[i + 1] == quote) { current.push_back(line[++i]); continue; }
            current.push_back(c);
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!current.empty()) { arguments.push_back(std::move(current)); current.clear(); }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) arguments.push_back(std::move(current));
    return arguments;
}


int hex_digit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return 10 + value - 'a';
    if (value >= 'A' && value <= 'F') return 10 + value - 'A';
    return -1;
}

std::optional<std::filesystem::path> decode_index_worker_path(std::string_view encoded) {
    if ((encoded.size() % 2u) != 0u) return std::nullopt;
    std::u8string utf8;
    utf8.reserve(encoded.size() / 2u);
    for (std::size_t i = 0; i < encoded.size(); i += 2u) {
        const int high = hex_digit(encoded[i]);
        const int low = hex_digit(encoded[i + 1u]);
        if (high < 0 || low < 0) return std::nullopt;
        utf8.push_back(static_cast<char8_t>((high << 4) | low));
    }
    return std::filesystem::path(utf8);
}

bool set_index_shard_mode(std::string_view mode) {
    if (mode != "auto" && mode != "1" && mode != "2" && mode != "4" && mode != "8" && mode != "16") return false;
    const std::string value(mode);
#if defined(_WIN32)
    return _putenv_s("SUCH_INDEX_SHARDS", value.c_str()) == 0;
#else
    return ::setenv("SUCH_INDEX_SHARDS", value.c_str(), 1) == 0;
#endif
}

int run_index_worker(int argc, char* const* argv, such::ui::PlatformDialect dialect) {
    if (argc < 3 || argv[2] == nullptr) return 2;
    const std::string operation = lower_ascii(argv[2]);

    std::vector<std::filesystem::path> roots;
    roots.reserve(argc > 3 ? static_cast<std::size_t>(argc - 3) : 0u);
    std::filesystem::path diagnostic_path;
    bool shard_option_seen = false;
    for (int i = 3; i < argc; ++i) {
        const std::string_view token = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (token.starts_with("--diagnostic=")) {
            const auto decoded = decode_index_worker_path(token.substr(13));
            if (!decoded || !diagnostic_path.empty()) return 2;
            diagnostic_path = *decoded;
            continue;
        }
        if (token.starts_with("--shards=")) {
            if (shard_option_seen || !set_index_shard_mode(token.substr(9))) return 2;
            shard_option_seen = true;
            continue;
        }
        const auto decoded = decode_index_worker_path(argv[i] ? std::string_view(argv[i]) : std::string_view{});
        if (!decoded.has_value()) return 2;
        roots.push_back(*decoded);
    }

    if (operation == "reindex" && !roots.empty()) return 2;
    if (operation == "add" && roots.size() != 1u) return 2;
    if (operation != "reindex" && operation != "add" && operation != "replace") return 2;

    std::ofstream diagnostic;
    if (!diagnostic_path.empty()) diagnostic.open(diagnostic_path, std::ios::trunc);
    const auto fail = [&](int code, const std::string& message) {
        std::fprintf(stderr, "%s\n", message.c_str());
        if (diagnostic) { diagnostic << message << '\n'; diagnostic.flush(); }
        return code;
    };
#if defined(_WIN32)
    _putenv_s("SUCH_INDEX_CONTINUE_ON_DOCUMENT_ERROR", "1");
#else
    ::setenv("SUCH_INDEX_CONTINUE_ON_DOCUMENT_ERROR", "1", 1);
#endif
    such::runtime::RuntimeClient runtime;
    std::string runtime_error;
    if (!runtime.load(&runtime_error)) return fail(5, "Index load failed: " + runtime_error);

    // Worker owns every operation that can start a disk crawl. The GUI keeps a
    // separate read/search handle and never executes root mutation or reindexing
    // in-process.
    such::runtime::IndexRootService root_service(runtime, dialect, true);
    bool ok = false;
    if (operation == "reindex") ok = root_service.reindex_async(&runtime_error);
    else if (operation == "add") ok = root_service.add_root(roots.front(), &runtime_error);
    else ok = root_service.replace_roots(roots, &runtime_error);
    if (!ok) return fail(6, "Index request failed: " + runtime_error);

    runtime.wait_for_idle();
    const auto crawl_status = runtime.status();
    if (crawl_status.indexing || !crawl_status.last_error.empty()) {
        return fail(8, "Index crawl failed: " + crawl_status.last_error);
    }

    // Root policy maintenance is also worker-owned. It is bounded per poll, so
    // drain it after the crawl while keeping all index-side work outside the GUI.
    constexpr std::size_t kMaintenancePollLimit = 4096u;
    std::size_t polls = 0;
    while (root_service.policy_maintenance_active() && polls < kMaintenancePollLimit) {
        root_service.poll_policy_maintenance();
        if (runtime.status().indexing) runtime.wait_for_idle();
        ++polls;
    }
    if (root_service.policy_maintenance_active()) return fail(7, "Index folder policy maintenance did not finish.");
    const auto final_status = runtime.status();
    if (final_status.indexing || !final_status.last_error.empty()) {
        return fail(8, "Index worker did not reach a readable idle state: " + final_status.last_error);
    }
    // Completion publication is still the Private Runtime's responsibility.
    // ABI v1 has no explicit transaction-commit receipt; worker exit and a
    // reloadable view are necessary checks, not proof of a durable commit.
    return 0;
}

int run_interactive(such::ui::PlatformDialect dialect) {
    print_help();
    std::string line;
    while (true) {
        std::printf("\nsuch> ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) { std::printf("\n"); return 0; }
        auto arguments = split_interactive_command(line);
        if (arguments.empty()) continue;
        const auto command = lower_ascii(arguments.front());
        if (command == "exit" || command == "quit") return 0;
        arguments.insert(arguments.begin(), "SuchCLI");
        std::vector<char*> argv;
        argv.reserve(arguments.size());
        for (auto& argument : arguments) argv.push_back(argument.data());
        const int result = run(static_cast<int>(argv.size()), argv.data(), dialect);
        if (result != 0) std::fprintf(stderr, "command exited with code %d\n", result);
    }
}

int mutate_file(such::runtime::RuntimeClient& runtime, std::string_view verb, const std::string& path) {
    std::string error;
    bool ok = false;
    if (verb == "pin") ok = runtime.set_pinned(path, true, &error);
    else if (verb == "unpin") ok = runtime.set_pinned(path, false, &error);
    else if (verb == "hide") ok = runtime.set_indexed(path, false, &error);
    else if (verb == "show") ok = runtime.set_indexed(path, true, &error);
    if (!ok) return print_runtime_error("file update failed", error, 8);
    std::printf("ok\n");
    return 0;
}

int run_legacy_index_command(
    such::runtime::RuntimeClient& runtime,
    std::string_view raw,
    such::ui::PlatformDialect dialect,
    std::string& runtime_error) {
    const auto command = such::ui::parse_index_command(raw, dialect);
    if (!command.matched) return -1;
    if (command.kind == such::ui::IndexCommandKind::Reindex && command.argument && !set_index_shard_mode(*command.argument)) return 2;
    such::runtime::IndexRootService root_service(runtime, dialect, false);
    switch (command.kind) {
        case such::ui::IndexCommandKind::AddRoot:
            if (!command.argument) { std::fprintf(stderr, "usage: such add <folder>\n"); return 2; }
            if (!root_service.add_root(*command.argument, &runtime_error)) return print_runtime_error("add root failed", runtime_error, 6);
            runtime.wait_for_idle();
            std::printf("files=%zu\n", runtime.status().indexed_files);
            return 0;
        case such::ui::IndexCommandKind::ReplaceRoot:
            if (!command.argument) { std::fprintf(stderr, "usage: such use <folder>\n"); return 2; }
            if (!root_service.replace_roots({std::filesystem::path(*command.argument)}, &runtime_error)) return print_runtime_error("use root failed", runtime_error, 6);
            runtime.wait_for_idle();
            std::printf("files=%zu\n", runtime.status().indexed_files);
            return 0;
        case such::ui::IndexCommandKind::Reindex:
            if (!root_service.reindex_async(&runtime_error))
                return print_runtime_error("reindex failed", runtime_error, 6);
            runtime.wait_for_idle();
            std::printf("files=%zu\n", runtime.status().indexed_files);
            return 0;
        case such::ui::IndexCommandKind::ShowRoots: {
            const auto roots = runtime.roots(&runtime_error);
            if (!runtime_error.empty()) return print_runtime_error("list roots failed", runtime_error, 6);
            for (const auto& root : roots) std::printf("%s\n", root.c_str());
            return 0;
        }
        case such::ui::IndexCommandKind::NoCommand:
            return -1;
    }
    return -1;
}

} // namespace

bool should_dispatch_from_gui(int argc, char* const* argv) noexcept {
    if (argc <= 1 || argv == nullptr || argv[1] == nullptr) return false;
    const std::string_view first(argv[1]);
    return first != "--smoke" && first != "--demo";
}

int run(int argc, char* const* argv, such::ui::PlatformDialect dialect) {
    if (argc <= 1) {
        if (stdin_is_terminal()) return run_interactive(dialect);
        print_help();
        return 0;
    }

    const std::string first_raw = argv[1] ? argv[1] : "";
    const std::string first = lower_ascii(first_raw);

    if (first == "--index-worker") return run_index_worker(argc, argv, dialect);

    if (is_verb(first, {"help", "--help", "-h"})) { print_help(); return 0; }
    if (is_verb(first, {"version", "--version", "-v"})) {
        std::printf("%.*s\n", static_cast<int>(such::version::kVersion.size()), such::version::kVersion.data());
        return 0;
    }
    if (is_verb(first, {"stop", "shutdown", "--shutdown"})) {
        bool force = false;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = lower_ascii(argv[i] ? argv[i] : "");
            if (arg == "--force" || arg == "-f") force = true;
            else { std::fprintf(stderr, "usage: such stop [--force]\n"); return 2; }
        }
        const auto result = stop_such_processes(force);
        if (!result.error.empty()) return print_runtime_error("stop failed", result.error, 9);
        if (result.matched == 0) { std::printf("No running Such processes.\n"); return 0; }
        std::printf("stopped=%zu", result.stopped);
        if (result.forced) std::printf(" forced=%zu", result.forced);
        if (result.failed) std::printf(" failed=%zu", result.failed);
        std::printf("\n");
        return result.failed == 0 ? 0 : 9;
    }

    // Font preference is public frontend state and does not require the private runtime.
    if (first == "font") {
        if (argc == 2) {
            for (const auto& option : such::ui::builtin_font_options()) std::printf("%s\t%s\n", option.alias.c_str(), option.family.c_str());
            std::printf("system\tsystem default\n");
            return 0;
        }
        const std::string requested = join_args(argc, argv, 2);
        const std::string lowered = lower_ascii(requested);
        std::string family;
        bool found = lowered == "system";
        if (!found) {
            for (const auto& option : such::ui::builtin_font_options()) {
                if (lower_ascii(option.alias) == lowered || lower_ascii(option.family) == lowered) {
                    family = option.family; found = true; break;
                }
            }
        }
        if (!found) { std::fprintf(stderr, "unknown font: %s\n", requested.c_str()); return 2; }
        std::string error;
        const bool ok = family.empty() ? such::ui::clear_font_preference(&error) : such::ui::save_font_preference(family, &error);
        if (!ok) return print_runtime_error("font failed", error, 4);
        std::printf("font=%s\n", family.empty() ? "system" : family.c_str());
        return 0;
    }

    const std::string raw_command = join_args(argc, argv, 1);
    if (const auto legacy_font = such::ui::parse_font_command(raw_command, dialect); legacy_font.matched) {
        if (legacy_font.show_picker) {
            for (const auto& option : such::ui::builtin_font_options()) std::printf("%s\t%s\n", option.alias.c_str(), option.family.c_str());
            std::printf("system\tsystem default\n");
            return 0;
        }
        if (legacy_font.requested_family) {
            std::string error;
            const bool ok = legacy_font.requested_family->empty()
                ? such::ui::clear_font_preference(&error)
                : such::ui::save_font_preference(*legacy_font.requested_family, &error);
            if (!ok) return print_runtime_error("font failed", error, 4);
            std::printf("font=%s\n", legacy_font.requested_family->empty() ? "system" : legacy_font.requested_family->c_str());
            return 0;
        }
    }

    auto lease = such::ui::FrontendLease::try_acquire(such::ui::FrontendMode::Cli);
    if (!lease.acquired()) {
        std::fprintf(stderr, "Such CLI unavailable: %s\n", lease.error().c_str());
        return 23;
    }

    if (first == "snapshot") {
        if (argc != 4 || argv[2] == nullptr || argv[3] == nullptr) {
            std::fprintf(stderr, "usage: such snapshot <export|import|info> <file.sidx>\n");
            return 2;
        }
        const std::string action = lower_ascii(argv[2]);
        const std::filesystem::path snapshot_path(argv[3]);
        std::string snapshot_error;
        if (action == "info") {
            such::runtime::IndexSnapshotInfo info;
            if (!such::runtime::inspect_index_snapshot(snapshot_path, &info, &snapshot_error))
                return print_runtime_error("snapshot inspect failed", snapshot_error, 10);
            std::printf("format=%u\ngeneration=%llu\nfiles=%u\nbytes=%llu\nroots=%zu\n",
                info.format_version,
                static_cast<unsigned long long>(info.generation),
                info.file_count,
                static_cast<unsigned long long>(info.payload_bytes),
                info.roots.size());
            for (const auto& root : info.roots) std::printf("root=%s\n", root.string().c_str());
            return 0;
        }

        const auto state_directory = such::runtime::index_state_directory(&snapshot_error);
        if (state_directory.empty()) return print_runtime_error("snapshot state resolution failed", snapshot_error, 10);

        if (action == "export") {
            std::vector<std::filesystem::path> roots;
            std::uint64_t generation = 0;
            {
                such::runtime::RuntimeClient snapshot_runtime;
                if (!snapshot_runtime.load(&snapshot_error))
                    return print_runtime_error("snapshot runtime load failed", snapshot_error, 5);
                auto status = snapshot_runtime.status();
                if (status.indexing) {
                    snapshot_runtime.wait_for_idle();
                    status = snapshot_runtime.status();
                }
                const auto runtime_roots = snapshot_runtime.roots(&snapshot_error);
                if (!snapshot_error.empty())
                    return print_runtime_error("snapshot root query failed", snapshot_error, 6);
                roots.reserve(runtime_roots.size());
                for (const auto& root : runtime_roots) roots.emplace_back(root);
                generation = status.generation;
            }
            such::runtime::IndexSnapshotInfo info;
            if (!such::runtime::export_index_snapshot(
                    state_directory, snapshot_path, roots, generation, &info, &snapshot_error))
                return print_runtime_error("snapshot export failed", snapshot_error, 10);
            std::printf("snapshot=%s\ngeneration=%llu\nfiles=%u\nbytes=%llu\n",
                snapshot_path.string().c_str(),
                static_cast<unsigned long long>(info.generation),
                info.file_count,
                static_cast<unsigned long long>(info.payload_bytes));
            return 0;
        }

        if (action == "import") {
            such::runtime::IndexSnapshotInfo info;
            if (!such::runtime::import_index_snapshot(
                    snapshot_path, state_directory, &info, &snapshot_error))
                return print_runtime_error("snapshot import failed", snapshot_error, 10);

            such::runtime::RuntimeClient imported_runtime;
            if (!imported_runtime.load(&snapshot_error))
                return print_runtime_error("imported snapshot runtime load failed", snapshot_error, 5);
            const auto imported_roots = imported_runtime.roots(&snapshot_error);
            if (!snapshot_error.empty())
                return print_runtime_error("imported snapshot root verification failed", snapshot_error, 6);
            if (imported_roots.size() != info.roots.size()) {
                std::fprintf(stderr, "snapshot import verification failed: root count mismatch\n");
                return 10;
            }
            for (std::size_t i = 0; i < imported_roots.size(); ++i) {
                if (std::filesystem::path(imported_roots[i]).lexically_normal() != info.roots[i].lexically_normal()) {
                    std::fprintf(stderr, "snapshot import verification failed: root mismatch\n");
                    return 10;
                }
            }
            const auto status = imported_runtime.status();
            std::printf("snapshot=%s\ngeneration=%llu\nfiles=%zu\nroots=%zu\n",
                snapshot_path.string().c_str(),
                static_cast<unsigned long long>(status.generation),
                status.indexed_files,
                imported_roots.size());
            return 0;
        }

        std::fprintf(stderr, "usage: such snapshot <export|import|info> <file.sidx>\n");
        return 2;
    }

    such::runtime::RuntimeClient runtime;
    std::string runtime_error;
    if (!runtime.load(&runtime_error)) return print_runtime_error("runtime load failed", runtime_error, 5);
    such::runtime::IndexRootService root_service(runtime, dialect, false);

    if (first == "root") {
        if (argc == 2) {
            const auto roots = runtime.roots(&runtime_error);
            if (!runtime_error.empty()) return print_runtime_error("list roots failed", runtime_error, 6);
            for (const auto& root : roots) std::printf("%s\n", root.c_str());
            return 0;
        }
        const bool add = argc >= 4 && std::string_view(argv[2]) == "+";
        const int path_start = add ? 3 : 2;
        const std::string root_path = join_args(argc, argv, path_start);
        if (root_path.empty()) { std::fprintf(stderr, "usage: such root [ + ] <folder>\n"); return 2; }
        const bool ok = add
            ? root_service.add_root(root_path, &runtime_error)
            : root_service.replace_roots({std::filesystem::path(root_path)}, &runtime_error);
        if (!ok) return print_runtime_error(add ? "add root failed" : "use root failed", runtime_error, 6);
        runtime.wait_for_idle();
        std::printf("files=%zu\n", runtime.status().indexed_files);
        return 0;
    }
    if (first == "roots") {
        const auto roots = runtime.roots(&runtime_error);
        if (!runtime_error.empty()) return print_runtime_error("list roots failed", runtime_error, 6);
        for (const auto& root : roots) std::printf("%s\n", root.c_str());
        return 0;
    }
    if (is_verb(first, {"use", "drive"})) {
        if (argc < 3) { std::fprintf(stderr, "usage: such use <folder>\n"); return 2; }
        const std::filesystem::path root(join_args(argc, argv, 2));
        if (!root_service.replace_roots({root}, &runtime_error)) return print_runtime_error("use root failed", runtime_error, 6);
        runtime.wait_for_idle();
        std::printf("files=%zu\n", runtime.status().indexed_files);
        return 0;
    }
    if (is_verb(first, {"add", "index"})) {
        if (argc < 3) { std::fprintf(stderr, "usage: such add <folder>\n"); return 2; }
        if (!root_service.add_root(join_args(argc, argv, 2), &runtime_error)) return print_runtime_error("add root failed", runtime_error, 6);
        runtime.wait_for_idle();
        std::printf("files=%zu\n", runtime.status().indexed_files);
        return 0;
    }
    if (is_verb(first, {"scan", "reindex"})) {
        if (!root_service.reindex_async(&runtime_error))
            return print_runtime_error("reindex failed", runtime_error, 6);
        runtime.wait_for_idle();
        const auto completed = runtime.status();
        if (completed.indexing || !completed.last_error.empty()) {
            return print_runtime_error("reindex completion failed", completed.last_error, 8);
        }
        such::runtime::DailyIndexRefresh daily_state(such::runtime::DailyIndexRefresh::default_state_file());
        std::string state_error;
        if (daily_state.load(&state_error) &&
            !daily_state.record_success(such::runtime::DailyIndexRefresh::utc_now(), &state_error)) {
            std::fprintf(stderr, "Daily refresh timestamp not saved: %s\n", state_error.c_str());
        } else if (!state_error.empty()) {
            std::fprintf(stderr, "Daily refresh timestamp not loaded: %s\n", state_error.c_str());
        }
        std::printf("files=%zu\n", completed.indexed_files);
        return 0;
    }
    if (first == "status") {
        const auto status = runtime.status();
        std::printf("indexing=%s\nfiles=%zu\ngeneration=%llu\nroots=%zu\n",
            status.indexing ? "yes" : "no", status.indexed_files,
            static_cast<unsigned long long>(status.generation), status.roots.size());
        if (!status.last_error.empty()) std::printf("error=%s\n", status.last_error.c_str());
        return 0;
    }
    if (is_verb(first, {"pin", "unpin", "hide", "show"})) {
        if (argc < 3) { std::fprintf(stderr, "usage: such %s <file>\n", first.c_str()); return 2; }
        return mutate_file(runtime, first, join_args(argc, argv, 2));
    }

    std::string query;
    if (is_verb(first, {"find", "search"})) query = join_args(argc, argv, 2);
    else query = join_args(argc, argv, 1);
    if (query.empty()) { std::fprintf(stderr, "usage: such <query>\n"); return 2; }

    // Preserve the old slash command syntax for scripts while keeping the new
    // human-facing verb form as the primary CLI.
    const int legacy = run_legacy_index_command(runtime, query, dialect, runtime_error);
    if (legacy >= 0) return legacy;

    std::string search_error;
    const auto results = runtime.search(query, dialect, 0, &search_error);
    if (!search_error.empty()) return print_runtime_error("search failed", search_error, 7);
    for (const auto& item : results) {
        std::printf("%s\t%s%s\n", item.filename.c_str(), item.path.c_str(), item.pinned ? "\tPIN" : "");
        if (!item.content_match) continue;
        if (!item.logical_name.empty()) std::printf("%s\n", item.logical_name.c_str());
        else if (item.page_number != 0) std::printf("p. %u\n", item.page_number);
        else if (item.slide_number != 0) std::printf("Slide %u\n", item.slide_number);
        else if (item.sheet_number != 0) std::printf("Sheet %u\n", item.sheet_number);
        else if (item.line_number != 0) std::printf("L%u\n", item.line_number);
        else if (item.byte_offset != 0) std::printf("@%llu\n", static_cast<unsigned long long>(item.byte_offset));
        if (!item.snippet.empty()) std::printf("%s\n", item.snippet.c_str());
    }
    return 0;
}

} // namespace such::cli

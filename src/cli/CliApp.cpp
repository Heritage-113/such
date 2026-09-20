#include <such/cli/CliApp.h>

#include <such/Version.h>
#include <such/cli/ProcessControl.h>
#include <such/runtime/RuntimeClient.h>
#include <such/ui/FontSettings.h>
#include <such/ui/FrontendLease.h>
#include <such/ui/IndexCommands.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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
        "  such scan             rebuild index\n"
        "  such status           show index status\n"
        "  such pin <file>       pin a file\n"
        "  such unpin <file>     unpin a file\n"
        "  such hide <file>      exclude a file\n"
        "  such show <file>      include a file\n"
        "  such font [name]      list/set font\n"
        "  such stop [--force]   stop Such processes\n"
        "  such version          print version\n"
        "\n"
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
    switch (command.kind) {
        case such::ui::IndexCommandKind::AddRoot:
            if (!command.argument) { std::fprintf(stderr, "usage: such add <folder>\n"); return 2; }
            if (!runtime.add_root(*command.argument, &runtime_error)) return print_runtime_error("add root failed", runtime_error, 6);
            runtime.wait_for_idle();
            std::printf("files=%zu\n", runtime.status().indexed_files);
            return 0;
        case such::ui::IndexCommandKind::ReplaceRoot:
            if (!command.argument) { std::fprintf(stderr, "usage: such use <folder>\n"); return 2; }
            if (!runtime.replace_roots({std::filesystem::path(*command.argument)}, &runtime_error)) return print_runtime_error("use root failed", runtime_error, 6);
            runtime.wait_for_idle();
            std::printf("files=%zu\n", runtime.status().indexed_files);
            return 0;
        case such::ui::IndexCommandKind::Reindex:
            runtime.reindex_async();
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
    if (argc <= 1) { print_help(); return 0; }

    const std::string first_raw = argv[1] ? argv[1] : "";
    const std::string first = lower_ascii(first_raw);

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

    such::runtime::RuntimeClient runtime;
    std::string runtime_error;
    if (!runtime.load(&runtime_error)) return print_runtime_error("runtime load failed", runtime_error, 5);

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
            ? runtime.add_root(root_path, &runtime_error)
            : runtime.replace_roots({std::filesystem::path(root_path)}, &runtime_error);
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
        if (!runtime.replace_roots({root}, &runtime_error)) return print_runtime_error("use root failed", runtime_error, 6);
        runtime.wait_for_idle();
        std::printf("files=%zu\n", runtime.status().indexed_files);
        return 0;
    }
    if (is_verb(first, {"add", "index"})) {
        if (argc < 3) { std::fprintf(stderr, "usage: such add <folder>\n"); return 2; }
        if (!runtime.add_root(join_args(argc, argv, 2), &runtime_error)) return print_runtime_error("add root failed", runtime_error, 6);
        runtime.wait_for_idle();
        std::printf("files=%zu\n", runtime.status().indexed_files);
        return 0;
    }
    if (is_verb(first, {"scan", "reindex"})) {
        runtime.reindex_async();
        runtime.wait_for_idle();
        std::printf("files=%zu\n", runtime.status().indexed_files);
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
    }
    return 0;
}

} // namespace such::cli

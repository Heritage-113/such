#include <such/runtime/RuntimeClient.h>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    const bool expect_failure = argc > 1 && std::string_view(argv[1]) == "--expect-load-failure";
    const bool expect_search_failure = argc > 1 && std::string_view(argv[1]) == "--expect-search-failure";
    const bool expect_roots_failure = argc > 1 && std::string_view(argv[1]) == "--expect-roots-failure";
    such::runtime::RuntimeClient runtime;
    std::string error;
    const bool loaded = runtime.load(&error);
    if (expect_failure) {
        if (loaded) return 10;
        if (runtime.available()) return 11;
        if (error.find("forced fake runtime load failure") == std::string::npos) return 12;
        // A failed load must leave a clean, retryable client rather than a
        // half-created runtime handle that frontends mistake for availability.
        std::cout << "runtime load-failure state PASS\n";
        return 0;
    }
    if (!loaded) { std::cerr << error << '\n'; return 1; }
    if (!runtime.available()) return 6;
    if (!runtime.add_root(std::filesystem::path("/stub"), &error)) return 2;
    if (expect_roots_failure) {
        error.clear();
        const auto roots = runtime.roots(&error);
        if (!roots.empty()) return 16;
        if (error.find("forced fake runtime roots failure") == std::string::npos) return 17;
        if (!runtime.available()) return 18;
        std::cout << "runtime roots-failure state PASS\n";
        return 0;
    }
    if (expect_search_failure) {
        const auto failed_rows = runtime.search("report", such::ui::PlatformDialect::UnixLike, 25);
        if (!failed_rows.empty()) return 13;
        if (runtime.last_error().find("forced fake runtime search failure") == std::string::npos) return 14;
        if (!runtime.available()) return 15;
        std::cout << "runtime search-failure state PASS\n";
        return 0;
    }
    const auto rows = runtime.search("report", such::ui::PlatformDialect::UnixLike, 25);
    if (rows.size() != 1 || rows.front().filename != "project_report.pdf") return 3;
    const auto status = runtime.status();
    if (status.indexed_files != 1 || runtime.observed_extensions().empty()) return 4;
    const auto stats = runtime.engine_stats();
    if (!stats.backend_active) return 5;
    std::cout << "runtime client ABI smoke PASS\n";
    return 0;
}

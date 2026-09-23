#include <such/ui/ResultView.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace such::ui {
namespace {

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return lower_ascii(haystack).find(lower_ascii(needle)) != std::string::npos;
}

ResultItem demo_item(
    std::uint64_t file_id,
    std::string filename,
    std::string path,
    std::string extension,
    bool pinned) {
    ResultItem item;
    item.file_id = file_id;
    item.filename = std::move(filename);
    item.path = std::move(path);
    item.extension = std::move(extension);
    item.pinned = pinned;
    return item;
}

} // namespace

std::vector<ResultItem> make_demo_results(
    std::string_view query,
    PlatformDialect dialect,
    std::int64_t now_unix_seconds) {
    std::array<ResultItem, 5> seed{};
    if (dialect == PlatformDialect::Windows) {
        seed = {{
            demo_item(1, "project_report.pdf", R"(X:\Project\Reports\2026\project_report.pdf)", "pdf", true),
            demo_item(2, "hospital_plan.dwg", R"(X:\Architecture\Competition\hospital_plan.dwg)", "dwg", false),
            demo_item(3, "SearchRuntime.cpp", R"(X:\Such\src\runtime\SearchRuntime.cpp)", "cpp", false),
            demo_item(4, "budget_summary.xlsx", R"(X:\Finance\Q3\budget_summary.xlsx)", "xlsx", true),
            demo_item(5, "weekly_notes.txt", R"(X:\Notes\weekly_notes.txt)", "txt", false),
        }};
    } else {
        seed = {{
            demo_item(1, "project_report.pdf", "/Project/Reports/2026/project_report.pdf", "pdf", true),
            demo_item(2, "hospital_plan.dwg", "/Architecture/Competition/hospital_plan.dwg", "dwg", false),
            demo_item(3, "SearchRuntime.cpp", "/Such/src/runtime/SearchRuntime.cpp", "cpp", false),
            demo_item(4, "budget_summary.xlsx", "/Finance/Q3/budget_summary.xlsx", "xlsx", true),
            demo_item(5, "weekly_notes.txt", "/Notes/weekly_notes.txt", "txt", false),
        }};
    }

    const SearchFilter filter = parse_search_query(query, dialect, now_unix_seconds);
    std::vector<ResultItem> out;
    for (auto item : seed) {
        if (!item.authorized) continue;
        if (filter.pinned_only && !item.pinned) continue;
        if (!filter.extensions.empty() &&
            std::find(filter.extensions.begin(), filter.extensions.end(), lower_ascii(item.extension)) == filter.extensions.end()) {
            continue;
        }
        if (!contains_ci(item.filename + " " + item.path, filter.text)) continue;
        out.push_back(std::move(item));
        if (out.size() == 5) break;
    }
    return out;
}

} // namespace such::ui

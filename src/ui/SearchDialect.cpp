#include <such/ui/SearchDialect.h>

#include <algorithm>
#include <array>
#include <iomanip>
#include <charconv>
#include <cctype>
#include <ctime>
#include <sstream>
#include <unordered_set>

namespace such::ui {
namespace {

constexpr std::int64_t kDay = 86400;

constexpr auto kBuiltInExtensions = std::to_array<std::string_view>({
    // CAD / BIM / geometry / point clouds
    "dwg", "dxf", "dwt", "dws", "dst", "3dm", "rvt", "rfa", "rte", "ifc", "ifczip",
    "skp", "dae", "fbx", "obj", "stl", "3mf", "step", "stp", "iges", "igs", "sat", "sab",
    "brep", "x_t", "x_b", "prt", "asm", "sldprt", "sldasm", "slddrw", "ipt", "iam", "idw",
    "dgn", "pln", "pla", "lcf", "gsm", "las", "laz", "e57", "pts", "ptx", "rcp", "rcs",
    "nwc", "nwd", "nwf", "shp", "geojson", "kml", "kmz",

    // Documents / publishing / design
    "pdf", "xps", "oxps", "epub", "txt", "md", "markdown", "rtf", "doc", "docx", "odt",
    "xls", "xlsx", "xlsm", "xlsb", "ods", "csv", "tsv", "ppt", "pptx", "odp",
    "ai", "eps", "svg", "svgz", "psd", "psb", "indd", "idml", "fig", "xd", "afdesign",
    "afphoto", "afpub",

    // Images / media
    "png", "jpg", "jpeg", "jpe", "gif", "webp", "bmp", "tif", "tiff", "heic", "heif",
    "avif", "raw", "dng", "cr2", "cr3", "nef", "arw", "orf", "rw2",
    "mp3", "wav", "flac", "aac", "m4a", "ogg", "opus", "mp4", "mov", "mkv", "avi", "webm",

    // Source / scripts / data / configuration
    "c", "cc", "cpp", "cxx", "h", "hh", "hpp", "hxx", "m", "mm", "rs", "go", "py", "pyw",
    "js", "mjs", "cjs", "ts", "tsx", "jsx", "java", "kt", "kts", "swift", "cs", "fs", "fsx",
    "rb", "php", "lua", "sh", "bash", "zsh", "fish", "ps1", "bat", "cmd", "sql", "r", "jl",
    "html", "htm", "css", "scss", "sass", "less", "xml", "json", "jsonl", "yaml", "yml", "toml",
    "ini", "cfg", "conf", "env", "gradle", "cmake", "make", "mk", "proto", "graphql",

    // Archives / packages / disk / binaries often searched by filename
    "zip", "7z", "rar", "tar", "gz", "bz2", "xz", "zst", "tgz", "tbz2", "txz", "iso", "dmg",
    "pkg", "deb", "rpm", "msi", "exe", "dll", "so", "dylib", "a", "lib", "jar", "war", "apk",
    "ipa", "appimage", "bin", "wasm"
});

std::string lower_ascii(std::string_view v) {
    std::string out(v);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string trim_ascii(std::string_view v) {
    std::size_t first = 0;
    while (first < v.size() && std::isspace(static_cast<unsigned char>(v[first])) != 0) ++first;
    std::size_t last = v.size();
    while (last > first && std::isspace(static_cast<unsigned char>(v[last - 1])) != 0) --last;
    return std::string(v.substr(first, last - first));
}

DetailSearch split_detail(std::string_view query) {
    DetailSearch out;
    std::size_t start = 0;
    bool first = true;
    while (true) {
        const std::size_t pos = query.find("/;", start);
        const std::string part = trim_ascii(query.substr(start, pos == std::string_view::npos ? query.size() - start : pos - start));
        if (first) { out.primary = part; first = false; }
        else { out.refinements.push_back(part); out.active = true; }
        if (pos == std::string_view::npos) break;
        start = pos + 2;
    }
    return out;
}

bool all_digits(std::string_view v) {
    return !v.empty() && std::all_of(v.begin(), v.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

bool localtime_safe(std::time_t value, std::tm& out) noexcept {
#if defined(_WIN32)
    return ::localtime_s(&out, &value) == 0;
#else
    return ::localtime_r(&value, &out) != nullptr;
#endif
}

std::optional<std::int64_t> local_epoch_for_ymd(int y, int m, int d) {
    if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const std::time_t epoch = std::mktime(&tm);
    if (epoch == static_cast<std::time_t>(-1)) return std::nullopt;

    // mktime normalizes invalid dates (e.g. February 31). Round-trip to reject
    // such tokens instead of silently consuming them as valid filters.
    std::tm check{};
    if (!localtime_safe(epoch, check)) return std::nullopt;
    if (check.tm_year != y - 1900 || check.tm_mon != m - 1 || check.tm_mday != d) return std::nullopt;
    return static_cast<std::int64_t>(epoch);
}

std::optional<std::int64_t> parse_iso_day_local(std::string_view s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
    const auto ys = s.substr(0, 4);
    const auto ms = s.substr(5, 2);
    const auto ds = s.substr(8, 2);
    if (!all_digits(ys) || !all_digits(ms) || !all_digits(ds)) return std::nullopt;
    int y = 0;
    int m = 0;
    int d = 0;
    const auto yr = std::from_chars(ys.data(), ys.data() + ys.size(), y);
    const auto mr = std::from_chars(ms.data(), ms.data() + ms.size(), m);
    const auto dr = std::from_chars(ds.data(), ds.data() + ds.size(), d);
    if (yr.ec != std::errc{} || mr.ec != std::errc{} || dr.ec != std::errc{}) return std::nullopt;
    return local_epoch_for_ymd(y, m, d);
}

std::optional<std::pair<std::int64_t, std::int64_t>> parse_iso_month_local(std::string_view s) {
    if (s.size() != 7 || s[4] != '-') return std::nullopt;
    const auto ys = s.substr(0, 4);
    const auto ms = s.substr(5, 2);
    if (!all_digits(ys) || !all_digits(ms)) return std::nullopt;
    int y = 0;
    int m = 0;
    const auto yr = std::from_chars(ys.data(), ys.data() + ys.size(), y);
    const auto mr = std::from_chars(ms.data(), ms.data() + ms.size(), m);
    if (yr.ec != std::errc{} || mr.ec != std::errc{} || m < 1 || m > 12) return std::nullopt;
    const auto begin = local_epoch_for_ymd(y, m, 1);
    const int next_y = m == 12 ? y + 1 : y;
    const int next_m = m == 12 ? 1 : m + 1;
    const auto end = local_epoch_for_ymd(next_y, next_m, 1);
    if (!begin || !end) return std::nullopt;
    return std::pair<std::int64_t, std::int64_t>{*begin, *end};
}

std::optional<std::pair<std::int64_t, std::int64_t>> local_day_range(std::int64_t now, int day_delta) {
    const std::time_t current = static_cast<std::time_t>(now);
    std::tm tm{};
    if (!localtime_safe(current, tm)) return std::nullopt;
    tm.tm_mday += day_delta;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    const std::time_t begin = std::mktime(&tm);
    if (begin == static_cast<std::time_t>(-1)) return std::nullopt;
    tm.tm_mday += 1;
    tm.tm_isdst = -1;
    const std::time_t end = std::mktime(&tm);
    if (end == static_cast<std::time_t>(-1)) return std::nullopt;
    return std::pair<std::int64_t, std::int64_t>{
        static_cast<std::int64_t>(begin), static_cast<std::int64_t>(end)};
}

std::optional<std::int64_t> parse_compact_day_local(std::string_view s, bool month_day_year) {
    if (s.size() != 8 || !all_digits(s)) return std::nullopt;
    int y = 0;
    int m = 0;
    int d = 0;
    auto parse_int = [](std::string_view value, int& out) {
        const auto r = std::from_chars(value.data(), value.data() + value.size(), out);
        return r.ec == std::errc{} && r.ptr == value.data() + value.size();
    };
    if (month_day_year) {
        if (!parse_int(s.substr(0, 2), m) || !parse_int(s.substr(2, 2), d) || !parse_int(s.substr(4, 4), y)) return std::nullopt;
    } else {
        if (!parse_int(s.substr(0, 4), y) || !parse_int(s.substr(4, 2), m) || !parse_int(s.substr(6, 2), d)) return std::nullopt;
    }
    return local_epoch_for_ymd(y, m, d);
}

std::optional<std::int64_t> parse_any_compact_day_local(std::string_view s) {
    if (const auto ymd = parse_compact_day_local(s, false)) return ymd;
    return parse_compact_day_local(s, true);
}

std::optional<std::pair<std::int64_t, std::int64_t>> parse_compact_range_local(std::string_view s) {
    if (s.size() != 17 || (s[8] != '-' && s[8] != '~')) return std::nullopt;
    const auto lhs = s.substr(0, 8);
    const auto rhs = s.substr(9, 8);
    std::optional<std::int64_t> begin;
    std::optional<std::int64_t> end;

    // Prefer one date order for the whole range. YYYYMMDD is tried first;
    // MMDDYYYY is accepted when the former is not a valid calendar date.
    begin = parse_compact_day_local(lhs, false);
    end = parse_compact_day_local(rhs, false);
    if (!begin || !end) {
        begin = parse_compact_day_local(lhs, true);
        end = parse_compact_day_local(rhs, true);
    }
    if (!begin || !end || *end < *begin) return std::nullopt;

    const auto next_day = local_day_range(*end, 1);
    const std::int64_t end_exclusive = next_day ? next_day->first : *end + kDay;
    return std::pair<std::int64_t, std::int64_t>{*begin, end_exclusive};
}

std::optional<std::pair<std::int64_t, std::int64_t>> parse_compact_day_as_range(std::string_view s) {
    const auto day = parse_any_compact_day_local(s);
    if (!day) return std::nullopt;
    const auto next = local_day_range(*day, 0);
    return std::pair<std::int64_t, std::int64_t>{*day, next ? next->second : *day + kDay};
}

bool looks_extension(std::string_view body) {
    if (body.empty() || body.size() > 16 || body.find(':') != std::string_view::npos) return false;
    return std::all_of(body.begin(), body.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '+';
    });
}

std::unordered_set<std::string> extension_registry(const std::vector<std::string>& observed_extensions) {
    std::unordered_set<std::string> out;
    out.reserve(kBuiltInExtensions.size() + observed_extensions.size());
    for (const auto ext : kBuiltInExtensions) out.emplace(ext);
    for (const auto& ext0 : observed_extensions) {
        std::string ext = lower_ascii(ext0);
        if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
        if (looks_extension(ext)) out.insert(std::move(ext));
    }
    return out;
}

bool is_valid_control(std::string_view body) {
    const std::string v = lower_ascii(body);
    if (v == "pin" || v == "today" || v == "yesterday" || v == "week" || v == "month" || v == "year") {
        return true;
    }
    if (v.starts_with("date:")) return parse_iso_day_local(std::string_view(v).substr(5)).has_value();
    if (v.starts_with("after:")) return parse_iso_day_local(std::string_view(v).substr(6)).has_value();
    if (v.starts_with("before:")) return parse_iso_day_local(std::string_view(v).substr(7)).has_value();
    if (parse_compact_range_local(v).has_value() || parse_compact_day_as_range(v).has_value()) return true;
    return parse_iso_day_local(v).has_value() || parse_iso_month_local(v).has_value();
}

void apply_date_token(SearchFilter& out, std::string_view body, std::int64_t now) {
    const std::string v = lower_ascii(body);
    if (v == "today" || v == "yesterday") {
        const auto relative_day_range = local_day_range(now, v == "today" ? 0 : -1);
        if (relative_day_range) {
            out.modified.after_unix_seconds = relative_day_range->first;
            out.modified.before_unix_seconds = relative_day_range->second;
        }
    } else if (v == "week") {
        out.modified.after_unix_seconds = now - 7 * kDay;
        out.modified.before_unix_seconds = now + 1;
    } else if (v == "month") {
        out.modified.after_unix_seconds = now - 30 * kDay;
        out.modified.before_unix_seconds = now + 1;
    } else if (v == "year") {
        out.modified.after_unix_seconds = now - 365 * kDay;
        out.modified.before_unix_seconds = now + 1;
    } else if (v.starts_with("date:")) {
        if (const auto d = parse_iso_day_local(std::string_view(v).substr(5))) {
            const auto explicit_day_range = local_day_range(*d, 0);
            out.modified.after_unix_seconds = *d;
            out.modified.before_unix_seconds = explicit_day_range ? explicit_day_range->second : *d + kDay;
        }
    } else if (v.starts_with("after:")) {
        if (const auto d = parse_iso_day_local(std::string_view(v).substr(6))) out.modified.after_unix_seconds = *d;
    } else if (v.starts_with("before:")) {
        if (const auto d = parse_iso_day_local(std::string_view(v).substr(7))) out.modified.before_unix_seconds = *d;
    } else if (const auto compact_range = parse_compact_range_local(v)) {
        out.modified.after_unix_seconds = compact_range->first;
        out.modified.before_unix_seconds = compact_range->second;
    } else if (const auto compact_day_range = parse_compact_day_as_range(v)) {
        out.modified.after_unix_seconds = compact_day_range->first;
        out.modified.before_unix_seconds = compact_day_range->second;
    } else if (const auto d = parse_iso_day_local(v)) {
        const auto iso_day_range = local_day_range(*d, 0);
        out.modified.after_unix_seconds = *d;
        out.modified.before_unix_seconds = iso_day_range ? iso_day_range->second : *d + kDay;
    } else if (const auto month = parse_iso_month_local(v)) {
        out.modified.after_unix_seconds = month->first;
        out.modified.before_unix_seconds = month->second;
    }
}

SearchFilter parse_impl(
    std::string_view query,
    PlatformDialect dialect,
    std::int64_t now_unix_seconds,
    const std::vector<std::string>& observed_extensions,
    bool collect_detail) {
    SearchFilter out;
    const std::string prefix(operator_prefix(dialect));
    const auto registered_extensions = extension_registry(observed_extensions);

    std::string normalized_query(query);
    DetailSearch detail;
    if (collect_detail) {
        detail = split_detail(query);
        if (detail.active) {
            normalized_query = detail.primary;
            for (const auto& refinement : detail.refinements) {
                if (!normalized_query.empty() && !refinement.empty()) normalized_query.push_back(' ');
                normalized_query += refinement;
            }
        }
    }
    std::istringstream in{normalized_query};
    std::string token;
    std::vector<std::string> text_parts;
    std::unordered_set<std::string> seen_ext;

    while (in >> token) {
        // Compact all-numeric date tokens are intentionally accepted without an
        // operator prefix as well as with / or //. Their shape is unambiguous
        // enough to avoid stealing ordinary filename text.
        std::string_view compact_candidate = token;
        if (compact_candidate.starts_with("//")) compact_candidate.remove_prefix(2);
        else if (compact_candidate.starts_with('/')) compact_candidate.remove_prefix(1);
        if (parse_compact_range_local(compact_candidate).has_value() ||
            parse_compact_day_as_range(compact_candidate).has_value()) {
            apply_date_token(out, compact_candidate, now_unix_seconds);
            continue;
        }

        const bool prefixed = token.rfind(prefix, 0) == 0;
        if (!prefixed || token.size() == prefix.size()) {
            text_parts.push_back(token);
            continue;
        }

        const std::string body = token.substr(prefix.size());
        const std::string lower = lower_ascii(body);
        if (lower == "pin") {
            out.pinned_only = true;
            continue;
        }
        if (is_valid_control(body)) {
            apply_date_token(out, body, now_unix_seconds);
            continue;
        }
        if (looks_extension(body) && registered_extensions.contains(lower)) {
            if (seen_ext.insert(lower).second) out.extensions.push_back(lower);
            continue;
        }

        // Unknown/unregistered slash token is deliberately preserved as ordinary text.
        text_parts.push_back(token);
    }

    for (std::size_t i = 0; i < text_parts.size(); ++i) {
        if (i) out.text.push_back(' ');
        out.text += text_parts[i];
    }

    if (collect_detail && detail.active) {
        for (const auto& refinement : detail.refinements) {
            const auto child = parse_impl(refinement, dialect, now_unix_seconds, observed_extensions, false);
            if (!child.text.empty()) out.detail_terms.push_back(child.text);
        }
    }
    return out;
}

} // namespace

DetailSearch parse_detail_search(std::string_view query) {
    return split_detail(query);
}

SearchFilter parse_search_query(std::string_view query, PlatformDialect dialect, std::int64_t now_unix_seconds) {
    return parse_impl(query, dialect, now_unix_seconds, {}, true);
}

SearchFilter parse_search_query(
    std::string_view query,
    PlatformDialect dialect,
    std::int64_t now_unix_seconds,
    const std::vector<std::string>& observed_extensions) {
    return parse_impl(query, dialect, now_unix_seconds, observed_extensions, true);
}

std::vector<Suggestion> autocomplete(
    std::string_view query,
    PlatformDialect dialect,
    const std::vector<std::string>& observed_extensions) {
    const std::string prefix(operator_prefix(dialect));
    const auto last_space = query.find_last_of(" \t\n");
    const std::string_view tail = last_space == std::string_view::npos ? query : query.substr(last_space + 1);

    // /claude, /codex and /; are deliberately universal UI commands. Linux and
    // macOS retain // for filesystem/index operators so absolute paths remain
    // unambiguous, but the AI/detail affordances follow the user's single-slash
    // muscle memory on every platform.
    if (tail.starts_with('/') && !tail.starts_with("//")) {
        std::vector<Suggestion> universal;
        const std::array<Suggestion, 3> commands{{
            {"/claude", "Open Claude with Such MCP"},
            {"/codex", "Open Codex with Such MCP"},
            {"/;", "Add detail-search branch"},
        }};
        const std::string typed = lower_ascii(tail);
        for (const auto& command : commands) {
            if (lower_ascii(command.token).starts_with(typed)) universal.push_back(command);
        }
        return universal;
    }

    if (tail.rfind(prefix, 0) != 0) return {};
    if (dialect == PlatformDialect::UnixLike && tail.size() < 2) return {};

    const std::string needle = lower_ascii(tail.substr(prefix.size()));
    std::vector<Suggestion> out;
    const std::array<Suggestion, 11> fixed{{
        {prefix + "font", "Font settings"},
        {prefix + "index", "Add indexed folder"},
        {prefix + "drive", "Replace search folder"},
        {prefix + "reindex", "Rebuild local index"},
        {prefix + "roots", "Indexed folders"},
        {prefix + "pin", "Pinned"},
        {prefix + "today", "Modified today"},
        {prefix + "yesterday", "Modified yesterday"},
        {prefix + "week", "Last 7 days"},
        {prefix + "month", "Last 30 days"},
        {prefix + "year", "Last 365 days"},
    }};
    auto maybe_add = [&](const Suggestion& suggestion) {
        const std::string body = lower_ascii(std::string_view(suggestion.token).substr(prefix.size()));
        if (body.starts_with(needle)) out.push_back(suggestion);
    };
    for (const auto& suggestion : fixed) maybe_add(suggestion);

    std::unordered_set<std::string> seen;
    for (const auto& ext0 : observed_extensions) {
        std::string ext = lower_ascii(ext0);
        if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
        if (!looks_extension(ext) || !seen.insert(ext).second || !ext.starts_with(needle)) continue;
        out.push_back({prefix + ext, "." + ext + " files"});
    }
    std::sort(out.begin(), out.end(), [](const Suggestion& a, const Suggestion& b) {
        return a.token < b.token;
    });
    if (out.size() > 8) out.resize(8);
    return out;
}


namespace {

std::string iso_local_day(std::int64_t epoch_seconds) {
    const std::time_t value = static_cast<std::time_t>(epoch_seconds);
    std::tm tm{};
    if (!localtime_safe(value, tm)) return {};
    char buffer[16]{};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm) == 0) return {};
    return buffer;
}

} // namespace

std::string compile_runtime_query(const SearchFilter& filter, PlatformDialect dialect) {
    const std::string prefix(operator_prefix(dialect));
    std::ostringstream out;
    bool first = true;
    auto append = [&](std::string_view value) {
        if (value.empty()) return;
        if (!first) out << ' ';
        out << value;
        first = false;
    };
    append(filter.text);
    for (const auto& ext : filter.extensions) append(prefix + ext);
    if (filter.pinned_only) append(prefix + "pin");
    if (filter.modified.after_unix_seconds.has_value()) {
        const auto day = iso_local_day(*filter.modified.after_unix_seconds);
        if (!day.empty()) append(prefix + "after:" + day);
    }
    if (filter.modified.before_unix_seconds.has_value()) {
        const auto day = iso_local_day(*filter.modified.before_unix_seconds);
        if (!day.empty()) append(prefix + "before:" + day);
    }
    return out.str();
}

} // namespace such::ui

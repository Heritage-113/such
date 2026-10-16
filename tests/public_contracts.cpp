#include <such/ui/AgentLauncher.h>
#include <such/ui/DisplayScalePolicy.h>
#include <such/ui/IndexCommands.h>
#include <such/ui/CommittedInputGate.h>
#include <such/ui/HangulQueryNfc.h>
#include <such/ui/SearchDialect.h>
#include <such/ui/ResponsiveLayout.h>
#include <such/runtime/IndexPathPolicy.h>
#include <such/runtime/SearchableFiles.h>

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <ctime>
#include <iostream>
#if defined(_WIN32)
#include <process.h>
#endif

namespace {
int fail(int code, const char* why) {
    std::cerr << "public contract failure " << code << ": " << why << '\n';
    return code;
}
}

int main() {
    using namespace such::ui;

    if (compose_hangul_query_nfc("\xE1\x84\x80\xE1\x85\xA5\xE1\x86\xB7\xE1\x84\x89\xE1\x85\xA2\xE1\x86\xA8") !=
        "\xEA\xB2\x80\xEC\x83\x89") return fail(78, "Hangul canonical NFC composition");
    if (compose_hangul_query_nfc("search /inside XYZ") != "search /inside XYZ") return fail(79, "ASCII query unchanged");

    // Native IME feeds transient preedit via EN_CHANGE on Windows; neither
    // intermediate Hangul nor a duplicate post-commit EN_CHANGE may search.
    CommittedInputGate<std::u16string> ime;
    if (!ime.accept(u"/inside ")) return fail(71, "initial committed query");
    ime.begin_composition();
    if (ime.accept(u"/inside ㄱ") || ime.accept(u"/inside 거") ||
        ime.accept(u"/inside 검")) return fail(72, "preedit triggered search");
    ime.end_composition();
    if (!ime.accept(u"/inside 검") || ime.accept(u"/inside 검"))
        return fail(73, "IME commit duplication");
    ime.begin_composition();
    if (ime.accept(u"/inside 검ㅅ")) return fail(74, "second preedit triggered search");
    ime.end_composition();
    if (!ime.accept(u"/inside 검색") || ime.accept(u"/inside 검색"))
        return fail(75, "second committed Hangul syllable");
    if (!ime.accept(u"/inside 검색X") || !ime.accept(u"/inside 검"))
        return fail(76, "mixed text and backspace");
    ime.begin_composition();
    ime.end_composition(); // cancelled composition does not itself dispatch.
    if (ime.accept(u"/inside 검")) return fail(77, "cancelled IME triggered search");

    if (!such::runtime::searchable_filename("main.cpp", "cpp") ||
        !such::runtime::searchable_filename("build.rs", "rs") ||
        !such::runtime::searchable_filename("CMakeLists.txt", "txt") ||
        !such::runtime::searchable_filename("Makefile", "") ||
        !such::runtime::searchable_filename(".gitignore", "") ||
        !such::runtime::searchable_filename("example.DWG", "DWG") ||
        !such::runtime::searchable_filename("report.pdf", "pdf"))
        return fail(80, "ordinary and developer files must remain searchable");
    if (!such::runtime::searchable_text_filename("main.cpp", "cpp") ||
        !such::runtime::searchable_text_filename("Makefile", "") ||
        !such::runtime::searchable_text_filename("settings.json", "json") ||
        such::runtime::searchable_text_filename("report.pdf", "pdf") ||
        such::runtime::searchable_text_filename("slides.pptx", "pptx") ||
        such::runtime::searchable_text_filename("image.png", "png") ||
        such::runtime::searchable_text_filename("mod.so", "so"))
        return fail(83, "content must be text only");
    if (such::runtime::searchable_filename("mystery.unknown987", "cpp") ||
        such::runtime::searchable_filename("internal.whatever", "") ||
        such::runtime::searchable_filename("blob", ""))
        return fail(81, "unregistered suffixes must be hidden from search");

    const auto w = parse_index_command("/drive X:\\Work", PlatformDialect::Windows);
    if (!w.matched || w.kind != IndexCommandKind::ReplaceRoot || !w.argument.has_value()) return fail(1, "Windows /drive");
    const auto u = parse_index_command("//drive /srv/work", PlatformDialect::UnixLike);
    if (!u.matched || u.kind != IndexCommandKind::ReplaceRoot || !u.argument.has_value()) return fail(2, "Unix //drive");
    const auto quoted = parse_index_command("//drive \"/media/My Drive\"", PlatformDialect::UnixLike);
    if (!quoted.argument.has_value() || *quoted.argument != "/media/My Drive") return fail(22, "quoted drive path");
    const auto quoted_single = parse_index_command("//index '/srv/Project Files'", PlatformDialect::UnixLike);
    if (!quoted_single.argument.has_value() || *quoted_single.argument != "/srv/Project Files") return fail(23, "single-quoted index path");

    const auto appdata_cmd = parse_index_command("%APPDATA%", PlatformDialect::Windows);
    if (!appdata_cmd.matched || appdata_cmd.kind != IndexCommandKind::AddRoot ||
        !appdata_cmd.argument.has_value() || *appdata_cmd.argument != "%appdata%")
        return fail(34, "manual APPDATA activation command");
    const auto local_appdata_cmd = parse_index_command("%LOCALAPPDATA%", PlatformDialect::Windows);
    if (!local_appdata_cmd.matched || local_appdata_cmd.kind != IndexCommandKind::AddRoot ||
        !local_appdata_cmd.argument.has_value() || *local_appdata_cmd.argument != "%localappdata%")
        return fail(35, "manual LOCALAPPDATA activation command");
    if (parse_index_command("%APPDATA%", PlatformDialect::UnixLike).matched)
        return fail(36, "APPDATA activation must remain Windows-only");

    const auto filter = parse_search_query("/pdf /week report", PlatformDialect::Windows, 1'800'000'000);
    if (filter.extensions.empty() || filter.text != "report") return fail(3, "legacy filter syntax");

    const std::string korean_query = "\xED\x95\x9C\xEA\xB8\x80 \xEA\xB2\x80\xEC\x83\x89";
    const auto korean_filter = parse_search_query(korean_query, PlatformDialect::Windows, 1'800'000'000);
    if (korean_filter.text != korean_query || compile_runtime_query(korean_filter, PlatformDialect::Windows) != korean_query)
        return fail(40, "UTF-8 Korean query preservation");

    // CAD extensions that users expect to find by filename are built in even if
    // an older private runtime has not yet reported them in list_extensions().
    const auto dwg = parse_search_query("/dwg Building-A", PlatformDialect::Windows, 1'800'000'000);
    if (dwg.extensions.size() != 1 || dwg.extensions.front() != "dwg" || dwg.text != "Building-A") return fail(4, "DWG extension filter");
    const auto dwg_unix = parse_search_query("//dwg Building-A", PlatformDialect::UnixLike, 1'800'000'000);
    if (dwg_unix.extensions.size() != 1 || dwg_unix.extensions.front() != "dwg") return fail(5, "Unix DWG extension filter");

    // /; is a public frontend refinement tree, not a private-runtime ABI feature.
    const auto detail = parse_detail_search("project /; structure /; 2026");
    if (!detail.active || detail.primary != "project" || detail.refinements.size() != 2 || detail.refinements[0] != "structure" || detail.refinements[1] != "2026") return fail(6, "detail tree parse");
    const auto detail_filter = parse_search_query("project /; structure /; 2026", PlatformDialect::Windows, 1'800'000'000);
    if (detail_filter.detail_terms.size() != 2 || detail_filter.detail_terms[0] != "structure" || detail_filter.detail_terms[1] != "2026") return fail(7, "detail conjunctive terms");

    // Compact numeric date forms: YYYYMMDD~YYYYMMDD, YYYYMMDD-YYYYMMDD,
    // MMDDYYYY-MMDDYYYY, and a bare eight-digit day.
    const auto ymd_tilde = parse_search_query("/20260901~20260919 invoice", PlatformDialect::Windows, 1'800'000'000);
    const auto ymd_dash = parse_search_query("/20260901-20260919 invoice", PlatformDialect::Windows, 1'800'000'000);
    const auto mdy_dash = parse_search_query("/09012026-09192026 invoice", PlatformDialect::Windows, 1'800'000'000);
    if (!ymd_tilde.modified.after_unix_seconds || !ymd_tilde.modified.before_unix_seconds) return fail(8, "YYYYMMDD~ range");
    if (ymd_tilde.modified.after_unix_seconds != ymd_dash.modified.after_unix_seconds || ymd_tilde.modified.before_unix_seconds != ymd_dash.modified.before_unix_seconds) return fail(9, "range delimiter equivalence");
    if (ymd_tilde.modified.after_unix_seconds != mdy_dash.modified.after_unix_seconds || ymd_tilde.modified.before_unix_seconds != mdy_dash.modified.before_unix_seconds) return fail(10, "MDY range equivalence");
    if (ymd_tilde.text != "invoice" || mdy_dash.text != "invoice") return fail(11, "date tokens removed from text");
    const auto bare_day = parse_search_query("20260919 report", PlatformDialect::Windows, 1'800'000'000);
    if (!bare_day.modified.after_unix_seconds || !bare_day.modified.before_unix_seconds || bare_day.text != "report") return fail(12, "bare numeric day");

    if (!parse_agent_command("/codex", PlatformDialect::Windows).matched) return fail(13, "Codex command");
    if (!parse_agent_command("/claude", PlatformDialect::Windows).matched) return fail(14, "Claude command");
    if (!parse_agent_command("//codex", PlatformDialect::UnixLike).matched) return fail(15, "Unix Codex command");
    if (!parse_agent_command("//claude", PlatformDialect::UnixLike).matched) return fail(16, "Unix Claude command");
    for (const auto mode : {"auto", "1", "2", "4", "8", "16"}) {
        const auto sharded = parse_index_command(std::string("/index ") + mode, PlatformDialect::Windows);
        if (!sharded.matched || sharded.kind != IndexCommandKind::Reindex || sharded.argument != mode)
            return fail(52, "index shard mode parsing");
    }
    const auto manual_index = parse_index_command(" /INDEX  ", PlatformDialect::Windows);
    if (!manual_index.matched || manual_index.kind != IndexCommandKind::Reindex || manual_index.argument.has_value())
        return fail(49, "bare Windows index manually rebuilds current roots");
    const auto add_index = parse_index_command("/index C:\\Work", PlatformDialect::Windows);
    if (!add_index.matched || add_index.kind != IndexCommandKind::AddRoot || !add_index.argument.has_value())
        return fail(50, "index with folder preserves add behavior");
    const auto index_suggestions = autocomplete("/index", PlatformDialect::Windows, {});
    if (index_suggestions.size() != 1 || index_suggestions.front().token != "/index")
        return fail(51, "manual index autocomplete");
    const auto drive_menu = autocomplete("/", PlatformDialect::Windows, {});
    bool has_drive = false;
    for (const auto& suggestion : drive_menu) has_drive = has_drive || suggestion.token == "/drive";
    if (!has_drive) return fail(41, "Windows slash menu must include folder picker");
    const auto drive_prefix = autocomplete("/dr", PlatformDialect::Windows, {});
    if (drive_prefix.size() != 1 || drive_prefix.front().token != "/drive") return fail(42, "Windows drive autocomplete");
    const auto drive_picker = parse_index_command("/drive", PlatformDialect::Windows);
    if (!drive_picker.matched || drive_picker.kind != IndexCommandKind::ReplaceRoot || drive_picker.argument.has_value())
        return fail(43, "Bare drive command opens folder picker");
    if (!autocomplete("/drive", PlatformDialect::UnixLike, {}).empty()) return fail(44, "Unix drive keeps double slash");
    const auto unix_agent_suggestions = autocomplete("/c", PlatformDialect::UnixLike, {});
    if (unix_agent_suggestions.size() < 2 || unix_agent_suggestions[0].token.empty()) return fail(17, "universal AI autocomplete");
    const auto detail_suggestions = autocomplete("/;", PlatformDialect::UnixLike, {});
    if (detail_suggestions.empty() || detail_suggestions.front().token != "/;") return fail(18, "detail autocomplete");

    const auto inside_windows = parse_search_query("/inside warranty", PlatformDialect::Windows, 1'800'000'000);
    if (inside_windows.scope != SearchScope::Content || inside_windows.text != "warranty") return fail(24, "Windows /inside scope");
    const auto inside_unix = parse_search_query("/inside warranty", PlatformDialect::UnixLike, 1'800'000'000);
    if (inside_unix.scope != SearchScope::Content || inside_unix.text != "warranty") return fail(25, "Unix universal /inside scope");
    const auto phrase_inside = parse_search_query("/inside \"force majeure\"", PlatformDialect::Windows, 1'800'000'000);
    if (phrase_inside.scope != SearchScope::Content || phrase_inside.text != "\"force majeure\"") return fail(26, "inside phrase preservation");
    const auto inside_suggestions = autocomplete("/in", PlatformDialect::UnixLike, {});
    if (inside_suggestions.empty() || inside_suggestions.front().token != "/inside") return fail(27, "inside autocomplete");


    if (!such::runtime::is_default_noise_directory_name("node_modules") ||
        !such::runtime::is_default_noise_directory_name(".git") ||
        !such::runtime::is_default_noise_directory_name("__pycache__") ||
        such::runtime::is_default_noise_directory_name("source")) return fail(28, "default noise directory names");
    if (!such::runtime::is_windows_manual_index_alias("%APPDATA%") ||
        !such::runtime::is_windows_manual_index_alias("%localappdata%") ||
        such::runtime::is_windows_manual_index_alias("APPDATA")) return fail(37, "Windows manual AppData aliases");
    if (!such::runtime::is_windows_protected_index_path("C:\\Windows\\System32") ||
        !such::runtime::is_windows_protected_index_path("c:/Program Files/Heritage") ||
        !such::runtime::is_windows_protected_index_path("C:\\Users\\alice\\AppData\\Local\\Google\\Chrome") ||
        !such::runtime::is_windows_protected_index_path("C:\\Projects\\demo\\node_modules\\pkg") ||
        such::runtime::is_windows_protected_index_path("C:\\Users\\alice\\Documents\\Project") ||
        such::runtime::is_windows_protected_index_path("D:\\Work\\AppDataModel")) return fail(29, "Windows protected index paths");
    if (!such::runtime::is_windows_such_install_path(
            "C:\\Users\\alice\\AppData\\Local\\Programs\\Such\\Such.exe") ||
        !such::runtime::is_windows_protected_index_path(
            "C:\\Users\\alice\\AppData\\Local\\Programs\\Such\\Such.exe") ||
        !such::runtime::is_windows_protected_index_path(
            "C:\\Users\\alice\\AppData\\Local\\Programs\\OtherApp\\cache.bin") ||
        !such::runtime::is_windows_protected_index_path(
            "C:\\Users\\alice\\AppData\\Local\\Programs\\Such\\node_modules\\pkg")) {
        return fail(38, "Windows AppData classification and Such install shape");
    }

    auto policy_parent = std::filesystem::temp_directory_path();
#if defined(_WIN32)
    // AppData\Local\Temp remains excluded from automatic indexing. CTest
    // provides a writable, non-protected parent without assuming that the
    // user's profile root accepts direct child creation.
    wchar_t* fixture_parent = nullptr;
    std::size_t fixture_parent_length = 0;
    if (_wdupenv_s(&fixture_parent, &fixture_parent_length, L"SUCH_TEST_POLICY_PARENT") != 0 ||
        fixture_parent == nullptr) {
        return fail(30, "policy fixture parent");
    }
    policy_parent = std::filesystem::path(fixture_parent);
    std::free(fixture_parent);
#endif
    const auto policy_root = policy_parent /
#if defined(_WIN32)
        ("such-policy-contract-" + std::to_string(_getpid()));
#else
        "such-policy-contract";
#endif
    std::error_code policy_ec;
    std::filesystem::remove_all(policy_root, policy_ec);
    std::filesystem::create_directories(policy_root / "src", policy_ec);
    std::filesystem::create_directories(policy_root / "node_modules" / "pkg", policy_ec);
    std::filesystem::create_directories(policy_root / ".git" / "objects", policy_ec);
    std::filesystem::create_directories(policy_root / "py" / "__pycache__", policy_ec);
    if (policy_ec) return fail(30, "policy fixture setup");
    const auto bounded_policy = such::runtime::plan_default_index_policy(policy_root);
    if (!bounded_policy.root_allowed || !bounded_policy.exclusions.empty()) {
        return fail(39, "bounded policy must be the safe default");
    }
    const auto policy = such::runtime::plan_default_index_policy(policy_root, true);
    if (!policy.root_allowed || policy.exclusions.size() < 3u) return fail(31, "policy subtree discovery");
    const auto rejected = such::runtime::plan_default_index_policy(policy_root / "node_modules", false);
    if (rejected.root_allowed) return fail(32, "noise root rejection");
    std::filesystem::remove_all(policy_root, policy_ec);


#if defined(_WIN32)
    const auto install_root = such::runtime::windows_such_install_root();
    if (install_root.empty()) return fail(33, "per-user Such install root resolution");
    const auto install_policy = such::runtime::plan_default_index_policy(install_root, false);
    if (!install_policy.root_allowed) return fail(33, "current-user Such install directory carve-out");
    const auto install_noise = such::runtime::plan_default_index_policy(install_root / L"node_modules", true);
    if (!install_noise.root_allowed || !install_noise.exclusions.empty()) return fail(45, "install exception outranks noise rules");
    const auto drive_policy = such::runtime::plan_default_index_policy(install_root.root_path(), false);
    if (!drive_policy.root_allowed) return fail(46, "whole drive root allowed");
    auto covers_install = [&](const std::filesystem::path& excluded) {
        const auto relative = install_root.lexically_relative(excluded);
        return relative.empty() ? install_root == excluded : *relative.begin() != L"..";
    };
    for (const auto& excluded : drive_policy.exclusions) {
        if (covers_install(excluded)) return fail(47, "drive policy must not mask installed Such");
    }
    const auto install_discovery = such::runtime::discover_default_noise_exclusions(install_root);
    if (!install_discovery.empty()) return fail(48, "installed subtree remains searchable during discovery");
    const auto sibling_app = install_root.parent_path() / L"OtherApp";
    const auto sibling_policy = such::runtime::plan_default_index_policy(sibling_app, false);
    if (sibling_policy.root_allowed) return fail(33, "AppData sibling must remain protected");
#endif

    const auto status_layout = compute_window_layout(760.0f, 600.0f);
    if (status_layout.status.y <= status_layout.search.y + status_layout.search.height)
        return fail(24, "Status row must sit below the search field");
    if (status_layout.results_viewport.y <= status_layout.status.y + status_layout.status.height)
        return fail(25, "Results must sit below the Status row");

    DisplayScaleInput linux_4k{};
    linux_4k.dpi = 96.0f; // common synthetic X11 value even on a 4K panel
    linux_4k.pixel_width = 3840;
    linux_4k.pixel_height = 2160;
    if (recommended_x11_ui_scale(linux_4k) < 2.35f) return fail(19, "4K X11 scale floor");
    DisplayScaleInput linux_3k{};
    linux_3k.dpi = 96.0f;
    linux_3k.pixel_width = 3136;
    linux_3k.pixel_height = 2048;
    if (recommended_x11_ui_scale(linux_3k) < 2.15f) return fail(20, "3K X11 scale floor");

    std::cout << "public contracts PASS\n";
    return 0;
}

#include <such/ui/AgentLauncher.h>
#include <such/ui/DisplayScalePolicy.h>
#include <such/ui/IndexCommands.h>
#include <such/ui/SearchDialect.h>
#include <such/ui/ResponsiveLayout.h>
#include <such/security/SecureSearchGate.h>
#include <such/runtime/IndexPathPolicy.h>

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
    const auto policy = such::runtime::plan_default_index_policy(policy_root, true);
    if (!policy.root_allowed || policy.exclusions.size() < 3u) return fail(31, "policy subtree discovery");
    const auto rejected = such::runtime::plan_default_index_policy(policy_root / "node_modules", false);
    if (rejected.root_allowed) return fail(32, "noise root rejection");
    std::filesystem::remove_all(policy_root, policy_ec);


    if (such::runtime::is_windows_protected_index_path("C:\\Heritage\\Such\\Such.exe"))
        return fail(33, "C:\\Heritage\\Such must not require a self-exclusion rule");

    const auto security_layout = compute_window_layout(760.0f, 600.0f);
    if (security_layout.security.y <= security_layout.search.y + security_layout.search.height)
        return fail(24, "Security row must sit below the search field");
    if (security_layout.results_viewport.y <= security_layout.security.y + security_layout.security.height)
        return fail(25, "Results must sit below the Security row");

    such::security::SecureSearchGate security_gate;
    std::string security_error;
    if (security_gate.request_enable(security_error) !=
        such::security::SecureSearchDecision::RequiresEnterpriseActivation)
        return fail(26, "Security gate must fail closed without Enterprise provider");
    if (security_gate.active()) return fail(27, "Security gate unexpectedly active");

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

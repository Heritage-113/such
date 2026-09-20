#include <such/ui/AgentLauncher.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#else
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace such::ui {
namespace {

constexpr std::string_view kSuchMcpRepository = "https://github.com/Heritage-113/suchMCP.git";

std::string trim_ascii(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
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
    std::error_code ec;
    const auto resolved = std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()), ec);
    return (ec ? std::filesystem::path(buffer.c_str()) : resolved).parent_path();
#else
    std::string buffer(PATH_MAX, '\0');
    const ssize_t len = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1u);
    if (len <= 0) return {};
    buffer.resize(static_cast<std::size_t>(len));
    return std::filesystem::path(buffer).parent_path();
#endif
}

std::string shell_quote_posix(std::string_view value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::filesystem::path sibling_runtime_path() {
#if defined(_WIN32)
    return executable_directory() / "SuchRuntimePrivate.dll";
#elif defined(__APPLE__)
    return executable_directory() / "libSuchRuntimePrivate.dylib";
#else
    return executable_directory() / "libSuchRuntimePrivate.so";
#endif
}

std::filesystem::path sibling_mcp_path() {
#if defined(_WIN32)
    return executable_directory() / "SuchMCP.exe";
#else
    return executable_directory() / "SuchMCP";
#endif
}

#if defined(_WIN32)
std::wstring ps_quote(std::wstring_view value) {
    std::wstring out = L"'";
    for (const wchar_t c : value) {
        if (c == L'\'') out += L"''";
        else out.push_back(c);
    }
    out.push_back(L'\'');
    return out;
}
std::wstring ps_quote(const std::filesystem::path& path) {
    const std::wstring native = path.wstring();
    return ps_quote(std::wstring_view(native));
}
#endif

std::string posix_mcp_bootstrap(const std::filesystem::path& sibling_mcp) {
    const std::string sibling_q = shell_quote_posix(sibling_mcp.string());
    std::ostringstream sh;
    sh << "MCP=" << sibling_q << "; ";
    sh << "CACHE=\"${XDG_CACHE_HOME:-$HOME/.cache}/heritage/such-mcp\"; REPO=\"$CACHE/repo\"; SRC=\"$CACHE/src\"; BUILD=\"$CACHE/build\"; CACHED=\"$CACHE/bin/SuchMCP\"; ";
    sh << "if [ ! -x \"$MCP\" ] && [ -x \"$CACHED\" ]; then MCP=\"$CACHED\"; fi; ";
    sh << "if [ ! -x \"$MCP\" ]; then ";
    sh << "command -v git >/dev/null 2>&1 || { echo 'Such: git is required to bootstrap SuchMCP.' >&2; exit 70; }; ";
    sh << "command -v cmake >/dev/null 2>&1 || { echo 'Such: cmake is required to bootstrap SuchMCP.' >&2; exit 70; }; ";
    sh << "mkdir -p \"$CACHE\"; ";
    sh << "if [ ! -d \"$REPO/.git\" ]; then git clone --depth 1 " << shell_quote_posix(kSuchMcpRepository) << " \"$REPO\" || exit $?; ";
    sh << "else git -C \"$REPO\" pull --ff-only >/dev/null 2>&1 || true; fi; ";
    sh << "BUILDROOT=\"$REPO\"; ";
    sh << "if [ ! -f \"$BUILDROOT/CMakeLists.txt\" ]; then ";
    sh << "ZIP=$(find \"$REPO\" -maxdepth 1 -type f -name 'SuchMCP*_SourceOnly.zip' -print | head -n 1); ";
    sh << "[ -n \"$ZIP\" ] || { echo 'Such: SuchMCP source archive not found.' >&2; exit 71; }; ";
    sh << "rm -rf \"$SRC\"; mkdir -p \"$SRC\"; (cd \"$SRC\" && cmake -E tar xf \"$ZIP\") || exit $?; ";
    sh << "CMAKEFILE=$(find \"$SRC\" -type f -name CMakeLists.txt -print | head -n 1); ";
    sh << "[ -n \"$CMAKEFILE\" ] || { echo 'Such: SuchMCP CMakeLists.txt not found after extraction.' >&2; exit 72; }; ";
    sh << "BUILDROOT=$(dirname \"$CMAKEFILE\"); fi; ";
    sh << "if [ -x \"$BUILDROOT/scripts/build_linux.sh\" ]; then bash \"$BUILDROOT/scripts/build_linux.sh\" --clean || exit $?; ";
    sh << "else rm -rf \"$BUILD\"; cmake -S \"$BUILDROOT\" -B \"$BUILD\" -DCMAKE_BUILD_TYPE=Release || exit $?; cmake --build \"$BUILD\" --config Release --parallel || exit $?; BUILDROOT=\"$BUILD\"; fi; ";
    sh << "BUILT=$(find \"$BUILDROOT\" -type f -name SuchMCP -print | head -n 1); ";
    sh << "[ -x \"$BUILT\" ] || { echo 'Such: SuchMCP build did not produce an executable.' >&2; exit 73; }; ";
    sh << "mkdir -p \"$(dirname \"$CACHED\")\"; cp \"$BUILT\" \"$CACHED\"; chmod 755 \"$CACHED\"; MCP=\"$CACHED\"; fi; ";
    return sh.str();
}

std::string posix_agent_script(AgentKind kind, const std::filesystem::path& cwd) {
    const auto mcp = sibling_mcp_path();
    const auto runtime = sibling_runtime_path();
    std::ostringstream sh;
    sh << "set -e; cd -- " << shell_quote_posix(cwd.string()) << "; ";
    sh << posix_mcp_bootstrap(mcp);
    sh << "RUNTIME=" << shell_quote_posix(runtime.string()) << "; ";
    sh << "[ -f \"$RUNTIME\" ] || { echo 'Such private runtime is missing beside the Such executable.' >&2; exit 74; }; ";
    if (kind == AgentKind::Codex) {
        sh << "command -v codex >/dev/null 2>&1 || { echo 'Such: Codex CLI is not installed.' >&2; exit 75; }; ";
        sh << "codex mcp remove such >/dev/null 2>&1 || true; ";
        sh << "codex mcp add such --env SUCH_RUNTIME_LIBRARY=\"$RUNTIME\" -- \"$MCP\"; exec codex";
    } else {
        sh << "command -v claude >/dev/null 2>&1 || { echo 'Such: Claude CLI is not installed.' >&2; exit 75; }; ";
        sh << "claude mcp remove such >/dev/null 2>&1 || true; ";
        sh << "claude mcp add --scope local --transport stdio such --env SUCH_RUNTIME_LIBRARY=\"$RUNTIME\" -- \"$MCP\"; exec claude";
    }
    return sh.str();
}

#if !defined(_WIN32) && !defined(__APPLE__)
bool command_available(const char* name) {
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string_view all(path);
    std::size_t start = 0;
    while (start <= all.size()) {
        const auto pos = all.find(':', start);
        const auto part = all.substr(start, pos == std::string_view::npos ? all.size() - start : pos - start);
        std::filesystem::path candidate = part.empty() ? std::filesystem::path(".") : std::filesystem::path(part);
        candidate /= name;
        if (::access(candidate.c_str(), X_OK) == 0) return true;
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return false;
}

bool spawn_linux_terminal(const std::string& script, std::string* error) {
    struct Candidate { const char* program; std::vector<std::string> args; };
    const std::vector<Candidate> candidates{
        {"x-terminal-emulator", {"-e", "bash", "-lc", script}},
        {"gnome-terminal", {"--", "bash", "-lc", script}},
        {"konsole", {"-e", "bash", "-lc", script}},
        {"kitty", {"bash", "-lc", script}},
        {"alacritty", {"-e", "bash", "-lc", script}},
        {"xfce4-terminal", {"--command", "bash -lc " + shell_quote_posix(script)}}
    };
    for (const auto& candidate : candidates) {
        if (!command_available(candidate.program)) continue;
        const pid_t pid = ::fork();
        if (pid < 0) continue;
        if (pid == 0) {
            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(candidate.program));
            for (const auto& arg : candidate.args) argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);
            ::execvp(candidate.program, argv.data());
            _exit(127);
        }
        return true;
    }
    if (error) *error = "No supported terminal emulator was found.";
    return false;
}
#endif

} // namespace

AgentCommand parse_agent_command(std::string_view query, PlatformDialect dialect) {
    const std::string text = lower_ascii(trim_ascii(query));
    const std::string prefix(operator_prefix(dialect));
    if (text == prefix + "claude" || text == "/claude") return {true, AgentKind::Claude};
    if (text == prefix + "codex" || text == "/codex") return {true, AgentKind::Codex};
    return {};
}

std::filesystem::path preferred_agent_working_directory(const std::vector<std::string>& roots) {
    for (const auto& value : roots) {
        std::error_code ec;
        std::filesystem::path p(value);
        if (std::filesystem::is_directory(p, ec) && !ec) return p;
    }
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path{} : cwd;
}

bool launch_agent_terminal(AgentKind kind, const std::filesystem::path& working_directory, std::string* error) {
    if (kind == AgentKind::None) {
        if (error) *error = "No AI agent was selected.";
        return false;
    }
    std::error_code ec;
    const auto cwd = (!working_directory.empty() && std::filesystem::is_directory(working_directory, ec) && !ec)
        ? working_directory : preferred_agent_working_directory({});

#if defined(_WIN32)
    const auto mcp = sibling_mcp_path();
    const auto runtime = sibling_runtime_path();
    std::wstring body;
    body += L"Set-Location -LiteralPath " + ps_quote(cwd) + L"; ";
    body += L"$mcp=" + ps_quote(mcp) + L"; $runtime=" + ps_quote(runtime) + L"; ";
    body += L"$cache=Join-Path $env:LOCALAPPDATA 'Heritage\\SuchMCP'; $cached=Join-Path $cache 'bin\\SuchMCP.exe'; if ((-not (Test-Path -LiteralPath $mcp)) -and (Test-Path -LiteralPath $cached)) { $mcp=$cached }; ";
    body += L"if (-not (Test-Path -LiteralPath $mcp)) { $repo=Join-Path $cache 'repo'; $src=Join-Path $cache 'src'; New-Item -ItemType Directory -Force $cache | Out-Null; ";
    body += L"if (-not (Test-Path (Join-Path $repo '.git'))) { git clone --depth 1 https://github.com/Heritage-113/suchMCP.git $repo } else { git -C $repo pull --ff-only | Out-Null }; ";
    body += L"$buildRoot=$repo; if (-not (Test-Path (Join-Path $buildRoot 'CMakeLists.txt'))) { $zip=Get-ChildItem $repo -Filter 'SuchMCP*_SourceOnly.zip' | Select-Object -First 1; if (-not $zip) { throw 'SuchMCP source archive not found' }; Remove-Item -Recurse -Force $src -ErrorAction SilentlyContinue; Expand-Archive -Force $zip.FullName $src; $cmakeFile=Get-ChildItem $src -Recurse -Filter CMakeLists.txt | Select-Object -First 1; if (-not $cmakeFile) { throw 'SuchMCP CMakeLists.txt not found' }; $buildRoot=Split-Path $cmakeFile.FullName -Parent }; ";
    body += L"$buildScript=Join-Path $buildRoot 'scripts\\build_windows.cmd'; if (Test-Path $buildScript) { & $buildScript -Clean; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; $searchRoot=$buildRoot } else { $build=Join-Path $cache 'build'; Remove-Item -Recurse -Force $build -ErrorAction SilentlyContinue; cmake -S $buildRoot -B $build -DCMAKE_BUILD_TYPE=Release; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; cmake --build $build --config Release --parallel; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; $searchRoot=$build }; $built=Get-ChildItem $searchRoot -Recurse -Filter SuchMCP.exe | Select-Object -First 1; if (-not $built) { throw 'SuchMCP build output not found' }; New-Item -ItemType Directory -Force (Split-Path $cached -Parent) | Out-Null; Copy-Item -Force $built.FullName $cached; $mcp=$cached }; ";
    body += L"if (-not (Test-Path -LiteralPath $runtime)) { throw 'Such private runtime is missing beside Such.exe' }; ";
    if (kind == AgentKind::Codex) {
        body += L"if (-not (Get-Command codex -ErrorAction SilentlyContinue)) { throw 'Such: Codex CLI is not installed.' }; codex mcp remove such *> $null; codex mcp add such --env ('SUCH_RUNTIME_LIBRARY=' + $runtime) -- $mcp; codex";
    } else {
        body += L"if (-not (Get-Command claude -ErrorAction SilentlyContinue)) { throw 'Such: Claude CLI is not installed.' }; claude mcp remove such *> $null; claude mcp add --scope local --transport stdio such --env ('SUCH_RUNTIME_LIBRARY=' + $runtime) -- $mcp; claude";
    }
    std::wstring command = L"powershell.exe -NoLogo -NoExit -ExecutionPolicy Bypass -Command \"";
    for (const wchar_t c : body) { if (c == L'\"') command += L"`\""; else command.push_back(c); }
    command += L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutable_cmd(command.begin(), command.end()); mutable_cmd.push_back(L'\0');
    const BOOL ok = CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                                   nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (!ok) { if (error) *error = "Could not open PowerShell for the requested AI agent."; return false; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
#elif defined(__APPLE__)
    (void)cwd;
    if (error) *error = "AI agent terminal launch is not supported on Apple mobile targets.";
    return false;
#else
    return spawn_linux_terminal(posix_agent_script(kind, cwd), error);
#endif
}

} // namespace such::ui

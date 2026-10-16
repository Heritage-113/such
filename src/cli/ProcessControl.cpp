#include <such/cli/ProcessControl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <cerrno>
#include <csignal>
#include <libproc.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <cerrno>
#include <csignal>
#include <fstream>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace such::cli {
namespace {

#if defined(_WIN32)
bool is_target_name(std::wstring_view name) noexcept {
    static constexpr std::array<std::wstring_view, 3> targets{{
        L"Such.exe", L"SuchCLI.exe", L"SuchMCP.exe"
    }};
    return std::any_of(targets.begin(), targets.end(), [&](std::wstring_view target) {
        if (name.size() != target.size()) return false;
        return CompareStringOrdinal(
            name.data(), static_cast<int>(name.size()),
            target.data(), static_cast<int>(target.size()), TRUE) == CSTR_EQUAL;
    });
}

#else
bool is_target_name(const std::string& name) {
    return name == "such" || name == "Such" || name == "SuchCLI" || name == "SuchMCP";
}
#endif

#if defined(_WIN32)
std::filesystem::path current_executable_path() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= buffer.size()) return {};
    buffer.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(buffer).lexically_normal();
}

std::optional<std::filesystem::path> process_executable_path(HANDLE process) {
    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length) == FALSE || length == 0) {
        return std::nullopt;
    }
    buffer.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(buffer).lexically_normal();
}

bool path_equal_or_within_ci(const std::filesystem::path& candidate,
                             const std::filesystem::path& root) {
    std::wstring candidate_text = candidate.lexically_normal().wstring();
    std::wstring root_text = root.lexically_normal().wstring();
    while (candidate_text.size() > 1u &&
           (candidate_text.back() == L'\\' || candidate_text.back() == L'/')) candidate_text.pop_back();
    while (root_text.size() > 1u &&
           (root_text.back() == L'\\' || root_text.back() == L'/')) root_text.pop_back();
    if (candidate_text.size() < root_text.size()) return false;
    if (CompareStringOrdinal(candidate_text.data(), static_cast<int>(root_text.size()),
                             root_text.data(), static_cast<int>(root_text.size()), TRUE) != CSTR_EQUAL) {
        return false;
    }
    if (candidate_text.size() == root_text.size()) return true;
    const wchar_t boundary = candidate_text[root_text.size()];
    return boundary == L'\\' || boundary == L'/';
}

std::vector<std::filesystem::path> trusted_process_roots() {
    std::vector<std::filesystem::path> roots;
    const auto self_path = current_executable_path();
    if (self_path.empty()) return roots;
    const auto self_dir = self_path.parent_path();
    roots.push_back(self_dir);
    const auto leaf = self_dir.filename().wstring();
    if (CompareStringOrdinal(leaf.data(), static_cast<int>(leaf.size()),
                             L"cli", 3, TRUE) == CSTR_EQUAL) {
        roots.push_back(self_dir.parent_path());
    }
    return roots;
}

bool is_trusted_process_path(const std::filesystem::path& candidate,
                             const std::vector<std::filesystem::path>& roots) {
    return std::any_of(roots.begin(), roots.end(), [&](const auto& root) {
        return path_equal_or_within_ci(candidate, root);
    });
}

struct CloseWindowContext {
    DWORD pid = 0;
    bool posted = false;
};

BOOL CALLBACK close_such_window(HWND hwnd, LPARAM value) {
    auto* context = reinterpret_cast<CloseWindowContext*>(value);
    if (context == nullptr) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == context->pid) {
        if (PostMessageW(hwnd, WM_CLOSE, 0, 0) != FALSE) context->posted = true;
    }
    return TRUE;
}
#elif !defined(__APPLE__)
std::filesystem::path read_proc_executable(pid_t pid) {
    const std::filesystem::path link = pid == ::getpid()
        ? std::filesystem::path("/proc/self/exe")
        : std::filesystem::path("/proc") / std::to_string(pid) / "exe";
    std::array<char, 4096> buffer{};
    const ssize_t length = ::readlink(link.c_str(), buffer.data(), buffer.size() - 1u);
    if (length <= 0) return {};
    buffer[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(buffer.data()).lexically_normal();
}

bool path_equal_or_within(const std::filesystem::path& candidate,
                          const std::filesystem::path& root) {
    const auto candidate_text = candidate.lexically_normal().generic_string();
    auto root_text = root.lexically_normal().generic_string();
    while (root_text.size() > 1u && root_text.back() == '/') root_text.pop_back();
    if (candidate_text == root_text) return true;
    return candidate_text.size() > root_text.size() && candidate_text.starts_with(root_text) &&
           candidate_text[root_text.size()] == '/';
}
#endif

#if !defined(_WIN32)
bool alive(pid_t pid) noexcept {
    if (::kill(pid, 0) == 0) return true;
    return errno != ESRCH;
}

bool wait_dead(pid_t pid, std::chrono::milliseconds timeout) noexcept {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (!alive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return !alive(pid);
}
#endif

} // namespace

StopResult stop_such_processes(bool force) noexcept {
    StopResult out;
#if defined(_WIN32)
    const DWORD self = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        out.error = "process snapshot failed";
        return out;
    }
    PROCESSENTRY32W pe{};
    pe.dwSize = static_cast<DWORD>(sizeof(pe));
    const auto trusted_roots = trusted_process_roots();
    if (trusted_roots.empty()) {
        CloseHandle(snapshot);
        out.error = "could not resolve the current Such executable path";
        return out;
    }
    std::vector<DWORD> pids;
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (pe.th32ProcessID == self) continue;
            if (!is_target_name(pe.szExeFile)) continue;
            HANDLE candidate_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (candidate_process == nullptr) continue;
            const auto candidate_path = process_executable_path(candidate_process);
            CloseHandle(candidate_process);
            if (candidate_path.has_value() && is_trusted_process_path(*candidate_path, trusted_roots)) {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    out.matched = pids.size();

    for (const DWORD pid : pids) {
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
        if (!process) { ++out.failed; continue; }
        bool terminated = false;
        if (!force) {
            CloseWindowContext context{pid, false};
            EnumWindows(close_such_window, reinterpret_cast<LPARAM>(&context));
            if (context.posted && WaitForSingleObject(process, 1500) == WAIT_OBJECT_0) terminated = true;
        }
        if (!terminated) {
            if (TerminateProcess(process, 0)) {
                WaitForSingleObject(process, 1000);
                terminated = true;
                ++out.forced;
            }
        }
        CloseHandle(process);
        if (terminated) ++out.stopped; else ++out.failed;
    }
#elif defined(__APPLE__)
    const pid_t self = ::getpid();
    const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bytes <= 0) { out.error = "proc_listpids failed"; return out; }
    std::vector<pid_t> pids(static_cast<std::size_t>(bytes / static_cast<int>(sizeof(pid_t))) + 16U);
    const int used = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (used <= 0) { out.error = "proc_listpids failed"; return out; }
    pids.resize(static_cast<std::size_t>(used / static_cast<int>(sizeof(pid_t))));
    for (const pid_t pid : pids) {
        if (pid <= 0 || pid == self) continue;
        char name[PROC_PIDPATHINFO_MAXSIZE]{};
        if (proc_name(pid, name, sizeof(name)) <= 0 || !is_target_name(name)) continue;
        ++out.matched;
        const int signal = force ? SIGKILL : SIGTERM;
        if (::kill(pid, signal) != 0) { ++out.failed; continue; }
        if (!force && !wait_dead(pid, std::chrono::milliseconds(1500))) {
            if (::kill(pid, SIGKILL) == 0) ++out.forced;
        }
        if (wait_dead(pid, std::chrono::milliseconds(500))) ++out.stopped; else ++out.failed;
    }
#else
    const pid_t self = ::getpid();
    const auto self_executable = read_proc_executable(self);
    if (self_executable.empty()) {
        out.error = "could not resolve the current Such executable path";
        return out;
    }
    const auto trusted_root = self_executable.parent_path();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        if (ec || !entry.is_directory(ec)) continue;
        const std::string leaf = entry.path().filename().string();
        if (leaf.empty() || !std::all_of(leaf.begin(), leaf.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) continue;
        pid_t pid = 0;
        try { pid = static_cast<pid_t>(std::stol(leaf)); } catch (...) { continue; }
        if (pid <= 0 || pid == self) continue;
        std::ifstream comm(entry.path() / "comm");
        std::string name;
        std::getline(comm, name);
        if (!is_target_name(name)) continue;
        const auto candidate_executable = read_proc_executable(pid);
        if (candidate_executable.empty() || !path_equal_or_within(candidate_executable, trusted_root)) continue;
        ++out.matched;
        const int signal = force ? SIGKILL : SIGTERM;
        if (::kill(pid, signal) != 0) { ++out.failed; continue; }
        if (!force && !wait_dead(pid, std::chrono::milliseconds(1500))) {
            if (::kill(pid, SIGKILL) == 0) ++out.forced;
        }
        if (wait_dead(pid, std::chrono::milliseconds(500))) ++out.stopped; else ++out.failed;
    }
#endif
    return out;
}

} // namespace such::cli

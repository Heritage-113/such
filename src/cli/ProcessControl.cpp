#include <such/cli/ProcessControl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
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
#include <filesystem>
#include <fstream>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace such::cli {
namespace {

bool is_target_name(const std::string& name) {
#if defined(_WIN32)
    auto lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower == "such.exe" || lower == "suchcli.exe" || lower == "suchmcp.exe";
#else
    return name == "such" || name == "Such" || name == "SuchCLI" || name == "SuchMCP";
#endif
}

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
    pe.dwSize = sizeof(pe);
    std::vector<DWORD> pids;
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (pe.th32ProcessID == self) continue;
            char utf8[MAX_PATH * 3]{};
            const int n = WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
            if (n > 0 && is_target_name(utf8)) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    out.matched = pids.size();

    struct CloseCtx { DWORD pid; bool posted; };
    auto close_windows = [](HWND hwnd, LPARAM value) -> BOOL {
        auto* ctx = reinterpret_cast<CloseCtx*>(value);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == ctx->pid) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            ctx->posted = true;
        }
        return TRUE;
    };

    for (const DWORD pid : pids) {
        HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
        if (!process) { ++out.failed; continue; }
        bool terminated = false;
        if (!force) {
            CloseCtx ctx{pid, false};
            EnumWindows(close_windows, reinterpret_cast<LPARAM>(&ctx));
            if (ctx.posted && WaitForSingleObject(process, 1500) == WAIT_OBJECT_0) terminated = true;
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

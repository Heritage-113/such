#include <such/runtime/IndexWorkerLauncher.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace such::runtime {
namespace {

std::filesystem::path executable_directory() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= buffer.size()) return {};
    buffer.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(buffer).parent_path();
#else
    std::string buffer(PATH_MAX, '\0');
    const ssize_t length = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1u);
    if (length <= 0) return {};
    buffer.resize(static_cast<std::size_t>(length));
    return std::filesystem::path(buffer).parent_path();
#endif
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::string hex_encode(std::string_view value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(value.size() * 2u);
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        out.push_back(digits[(byte >> 4u) & 0x0fu]);
        out.push_back(digits[byte & 0x0fu]);
    }
    return out;
}

const char* operation_name(IndexWorkerOperation operation) noexcept {
    switch (operation) {
        case IndexWorkerOperation::Reindex: return "reindex";
        case IndexWorkerOperation::AddRoot: return "add";
        case IndexWorkerOperation::ReplaceRoots: return "replace";
    }
    return "reindex";
}

std::vector<std::string> worker_arguments(const IndexWorkerRequest& request) {
    std::vector<std::string> args;
    args.emplace_back("--index-worker");
    args.emplace_back(operation_name(request.operation));
    if (request.shard_count >= 0) args.emplace_back("--shards=" + (request.shard_count == 0 ? std::string("auto") : std::to_string(request.shard_count)));
    for (const auto& root : request.roots) {
        args.emplace_back(hex_encode(path_utf8(root)));
    }
    return args;
}

#if defined(_WIN32)
std::wstring widen_ascii(std::string_view value) {
    std::wstring out;
    out.reserve(value.size());
    for (const char ch : value) {
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::wstring quote_windows_argument(std::wstring_view value) {
    if (value.empty()) return L"\"\"";
    if (value.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) return std::wstring(value);

    std::wstring out;
    out.push_back(L'\"');
    std::size_t slashes = 0;
    for (const wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
            continue;
        }
        if (ch == L'\"') {
            out.append(slashes * 2u + 1u, L'\\');
            out.push_back(L'\"');
            slashes = 0;
            continue;
        }
        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(ch);
    }
    out.append(slashes * 2u, L'\\');
    out.push_back(L'\"');
    return out;
}
#endif

} // namespace

namespace {

bool validate_worker_request(const IndexWorkerRequest& request, std::string* error) {
    const auto shards = request.shard_count;
    if (shards != -1 && shards != 0 && shards != 1 && shards != 2 && shards != 4 && shards != 8 && shards != 16) {
        if (error != nullptr) *error = "index shards must be auto, 1, 2, 4, 8 or 16";
        return false;
    }
    if (request.operation == IndexWorkerOperation::AddRoot && request.roots.size() != 1u) {
        if (error != nullptr) *error = "index worker add requires exactly one root";
        return false;
    }
    if (request.operation == IndexWorkerOperation::Reindex && !request.roots.empty()) {
        if (error != nullptr) *error = "index worker reindex does not accept roots";
        return false;
    }
    return true;
}

} // namespace

bool launch_index_worker_tracked(
    const IndexWorkerRequest& request,
    IndexWorkerTicket* ticket,
    std::string* error) {
    if (ticket == nullptr) {
        if (error != nullptr) *error = "index worker ticket is required";
        return false;
    }
    close_index_worker_ticket(ticket);
    if (!validate_worker_request(request, error)) return false;

    const auto directory = executable_directory();
    if (directory.empty()) {
        if (error != nullptr) *error = "could not resolve Such executable directory";
        return false;
    }
#if defined(_WIN32)
    const auto cli_path = directory / L"SuchCLI.exe";
#else
    const auto cli_path = directory / "SuchCLI";
#endif
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(cli_path, exists_error) || exists_error) {
        if (error != nullptr) *error = "index worker executable is missing beside Such";
        return false;
    }

    auto args = worker_arguments(request);
#if defined(_WIN32)
    std::error_code log_error;
    auto log_dir = std::filesystem::temp_directory_path(log_error) / L"Such";
    if (!log_error) std::filesystem::create_directories(log_dir, log_error);
    ticket->diagnostic_path.clear();
    if (!log_error) {
        ticket->diagnostic_path = log_dir / (L"index-worker-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L".log");
        args.emplace_back("--diagnostic=" + hex_encode(path_utf8(ticket->diagnostic_path)));
    }
    std::wstring command = quote_windows_argument(cli_path.wstring());
    for (const auto& arg : args) {
        command.push_back(L' ');
        command += quote_windows_argument(widen_ascii(arg));
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        cli_path.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        nullptr, directory.c_str(), &startup, &process);
    if (!created) {
        if (error != nullptr) *error = "could not start the Such index worker";
        return false;
    }
    CloseHandle(process.hThread);
    ticket->native_process = reinterpret_cast<std::uintptr_t>(process.hProcess);
    return true;
#else
    const pid_t worker = ::fork();
    if (worker < 0) {
        if (error != nullptr) *error = "could not fork the Such index worker";
        return false;
    }
    if (worker == 0) {
        if (::setsid() < 0) _exit(126);
        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)::dup2(null_fd, STDIN_FILENO);
            (void)::dup2(null_fd, STDOUT_FILENO);
            (void)::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) ::close(null_fd);
        }

        std::vector<std::string> storage;
        storage.reserve(args.size() + 1u);
        storage.push_back(cli_path.string());
        storage.insert(storage.end(), args.begin(), args.end());
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1u);
        for (auto& value : storage) argv.push_back(value.data());
        argv.push_back(nullptr);
        ::execv(cli_path.c_str(), argv.data());
        _exit(127);
    }
    ticket->native_process = static_cast<std::uintptr_t>(worker);
    return true;
#endif
}

bool launch_index_worker(const IndexWorkerRequest& request, std::string* error) {
#if defined(_WIN32)
    IndexWorkerTicket ticket{};
    if (!launch_index_worker_tracked(request, &ticket, error)) return false;
    close_index_worker_ticket(&ticket);
    return true;
#else
    if (!validate_worker_request(request, error)) return false;
    const auto directory = executable_directory();
    if (directory.empty()) {
        if (error != nullptr) *error = "could not resolve Such executable directory";
        return false;
    }
    const auto cli_path = directory / "SuchCLI";
    std::error_code exists_error;
    if (!std::filesystem::is_regular_file(cli_path, exists_error) || exists_error) {
        if (error != nullptr) *error = "index worker executable is missing beside Such";
        return false;
    }
    const auto args = worker_arguments(request);
    const pid_t launcher = ::fork();
    if (launcher < 0) {
        if (error != nullptr) *error = "could not fork the Such index worker";
        return false;
    }
    if (launcher == 0) {
        if (::setsid() < 0) _exit(126);
        const pid_t worker = ::fork();
        if (worker < 0) _exit(126);
        if (worker > 0) _exit(0);
        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)::dup2(null_fd, STDIN_FILENO);
            (void)::dup2(null_fd, STDOUT_FILENO);
            (void)::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) ::close(null_fd);
        }
        std::vector<std::string> storage;
        storage.reserve(args.size() + 1u);
        storage.push_back(cli_path.string());
        storage.insert(storage.end(), args.begin(), args.end());
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1u);
        for (auto& value : storage) argv.push_back(value.data());
        argv.push_back(nullptr);
        ::execv(cli_path.c_str(), argv.data());
        _exit(127);
    }
    int status = 0;
    while (::waitpid(launcher, &status, 0) < 0) {
        if (errno == EINTR) continue;
        if (error != nullptr) *error = "could not detach the Such index worker";
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (error != nullptr) *error = "Such index worker detachment failed";
        return false;
    }
    return true;
#endif
}

IndexWorkerPoll poll_index_worker(IndexWorkerTicket* ticket, int* exit_code) noexcept {
    if (exit_code != nullptr) *exit_code = 0;
    if (ticket == nullptr || !ticket->valid()) return IndexWorkerPoll::Succeeded;
#if defined(_WIN32)
    HANDLE process = reinterpret_cast<HANDLE>(ticket->native_process);
    const DWORD wait = WaitForSingleObject(process, 0);
    if (wait == WAIT_TIMEOUT) return IndexWorkerPoll::Running;
    DWORD code = 1;
    if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(process, &code)) code = 1;
    CloseHandle(process);
    ticket->native_process = 0;
    if (exit_code != nullptr) *exit_code = static_cast<int>(code);
    return code == 0 ? IndexWorkerPoll::Succeeded : IndexWorkerPoll::Failed;
#else
    const pid_t pid = static_cast<pid_t>(ticket->native_process);
    int status = 0;
    const pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == 0) return IndexWorkerPoll::Running;
    ticket->native_process = 0;
    if (result < 0) {
        if (exit_code != nullptr) *exit_code = errno;
        return IndexWorkerPoll::Failed;
    }
    int code = 1;
    if (WIFEXITED(status)) code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) code = 128 + WTERMSIG(status);
    if (exit_code != nullptr) *exit_code = code;
    return code == 0 ? IndexWorkerPoll::Succeeded : IndexWorkerPoll::Failed;
#endif
}

void close_index_worker_ticket(IndexWorkerTicket* ticket) noexcept {
    if (ticket == nullptr || !ticket->valid()) return;
#if defined(_WIN32)
    CloseHandle(reinterpret_cast<HANDLE>(ticket->native_process));
#endif
    ticket->native_process = 0;
}

} // namespace such::runtime

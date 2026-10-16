#include <such/runtime/DailyIndexRefresh.h>

#include <cstdlib>
#include <fstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace such::runtime {
namespace {
void fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}
std::filesystem::path home_state() {
#if defined(_WIN32)
    std::wstring path(32768, L'\0');
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", path.data(), static_cast<DWORD>(path.size()));
    if (length > 0 && static_cast<std::size_t>(length) < path.size()) return std::filesystem::path(path.substr(0, length)) / L"Such";
    length = GetEnvironmentVariableW(L"APPDATA", path.data(), static_cast<DWORD>(path.size()));
    if (length > 0 && static_cast<std::size_t>(length) < path.size()) return std::filesystem::path(path.substr(0, length)) / L"Such";
#else
    if (const char* value = std::getenv("XDG_STATE_HOME")) {
        if (*value != '\0') return std::filesystem::path(value) / "such";
    }
    if (const char* value = std::getenv("HOME")) {
        if (*value != '\0') return std::filesystem::path(value) / ".local" / "state" / "such";
    }
#endif
    return {};
}
} // namespace

DailyIndexRefresh::DailyIndexRefresh(std::filesystem::path path) : state_file_(std::move(path)) {}
std::filesystem::path DailyIndexRefresh::default_state_file() {
    const auto parent = home_state();
    return parent.empty() ? std::filesystem::path{} : parent / "daily-index-v1.state";
}
std::int64_t DailyIndexRefresh::utc_now() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
}
bool DailyIndexRefresh::load(std::string* error) {
    last_attempt_ = 0;
    last_success_ = 0;
    if (state_file_.empty()) { fail(error, "No persistent user state directory"); return false; }
    std::error_code ec;
    if (!std::filesystem::exists(state_file_, ec)) {
        if (ec) { fail(error, "Cannot inspect daily refresh state"); return false; }
        return true; // A previous completion is unknown: eligible at next normal use.
    }
    std::ifstream in(state_file_);
    std::string magic;
    std::int64_t success = 0, attempt = 0;
    if (!(in >> magic >> success >> attempt) || magic != "SUCH_DAILY_1" ||
        success < 0 || attempt < 0) {
        fail(error, "Malformed daily refresh state (preserved, not overwritten)");
        return false;
    }
    in >> std::ws;
    if (!in.eof()) { fail(error, "Trailing data in daily refresh state"); return false; }
    last_success_ = success;
    last_attempt_ = attempt;
    return true;
}
bool DailyIndexRefresh::due(std::int64_t now, bool has_roots, bool worker_active) const noexcept {
    if (now <= 0 || !has_roots || worker_active) return false;
    if (last_success_ > now) return false; // Clock rollback: never reindex repeatedly.
    if (last_success_ != 0 && now - last_success_ < kRefreshSeconds) return false;
    if (last_attempt_ > now) return false;
    return last_attempt_ == 0 || now - last_attempt_ >= kRetrySeconds;
}
bool DailyIndexRefresh::persist(std::string* error) {
    if (state_file_.empty()) { fail(error, "No persistent user state directory"); return false; }
    std::error_code ec;
    std::filesystem::create_directories(state_file_.parent_path(), ec);
    if (ec) { fail(error, "Cannot create daily refresh state directory"); return false; }
    auto temp = state_file_;
    temp += ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !(out << "SUCH_DAILY_1 " << last_success_ << ' ' << last_attempt_ << '\n') || !out.flush()) {
            fail(error, "Cannot write daily refresh state"); return false;
        }
    }
#if defined(_WIN32)
    // std::filesystem::rename() does not replace the target on all Windows filesystems.
    if (!MoveFileExW(temp.c_str(), state_file_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fail(error, "Cannot atomically publish daily refresh state"); return false;
    }
#else
    std::filesystem::rename(temp, state_file_, ec);
    if (ec) { fail(error, "Cannot atomically publish daily refresh state"); return false; }
#endif
    return true;
}
bool DailyIndexRefresh::record_attempt(std::int64_t now, std::string* error) {
    if (now <= 0) return false;
    last_attempt_ = now;
    return persist(error);
}
bool DailyIndexRefresh::record_success(std::int64_t now, std::string* error) {
    if (now <= 0) return false;
    last_success_ = now;
    last_attempt_ = now;
    return persist(error);
}
bool DailyIndexRefresh::record_failure(std::int64_t now, std::string* error) {
    if (now <= 0) return false;
    last_attempt_ = now;
    return persist(error);
}
} // namespace such::runtime

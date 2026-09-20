#include <such/ui/FrontendLease.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <utility>

namespace such::ui {

FrontendLease::FrontendLease(FrontendLease&& other) noexcept { *this = std::move(other); }
FrontendLease& FrontendLease::operator=(FrontendLease&& other) noexcept {
    if (this == &other) return *this;
    release();
    mode_ = other.mode_;
    acquired_ = other.acquired_;
    error_ = std::move(other.error_);
#if defined(_WIN32)
    handle_ = other.handle_; other.handle_ = nullptr;
#else
    fd_ = other.fd_; other.fd_ = -1;
#endif
    other.acquired_ = false;
    return *this;
}
FrontendLease::~FrontendLease() { release(); }

FrontendLease FrontendLease::try_acquire(FrontendMode mode) {
    FrontendLease lease;
    lease.mode_ = mode;
    // CLI commands and MCP servers are one-shot/service clients of the same
    // runtime and are designed to coexist with the GUI. The singleton lease is
    // therefore a GUI-window lease only: it prevents duplicate interactive GUI
    // owners without making terminal automation unusable while Such is open.
    if (mode != FrontendMode::Gui) {
        lease.acquired_ = true;
        return lease;
    }
#if defined(_WIN32)
    HANDLE h = CreateMutexW(nullptr, FALSE, L"Local\\Heritage.Such.Frontend");
    if (!h) { lease.error_ = "CreateMutexW failed"; return lease; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        lease.error_ = "another Such frontend is already running";
        return lease;
    }
    lease.handle_ = h;
    lease.acquired_ = true;
#else
    const uid_t uid = ::getuid();
    auto secure_runtime_directory = [uid](const char* value) -> std::string {
        if (value == nullptr || *value == '\0') return {};
        struct stat st{};
        if (::lstat(value, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != uid) return {};
        if ((st.st_mode & 0022) != 0 || ::access(value, W_OK | X_OK) != 0) return {};
        return value;
    };

    std::string directory = secure_runtime_directory(std::getenv("XDG_RUNTIME_DIR"));
    if (directory.empty()) {
        directory = "/tmp/heritage-such-" + std::to_string(static_cast<unsigned long long>(uid));
        if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
            lease.error_ = "create private frontend-lock directory failed";
            return lease;
        }
        struct stat st{};
        if (::lstat(directory.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != uid || (st.st_mode & 0077) != 0) {
            lease.error_ = "frontend-lock directory is not private to the current user";
            return lease;
        }
    }

    const std::string path = directory + "/heritage-such-frontend.lock";
    int flags = O_CREAT | O_RDWR | O_CLOEXEC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int fd = ::open(path.c_str(), flags, 0600);
    if (fd < 0) { lease.error_ = "open frontend lock failed"; return lease; }
    struct stat lock_st{};
    if (::fstat(fd, &lock_st) != 0 || !S_ISREG(lock_st.st_mode) || lock_st.st_uid != uid) {
        ::close(fd);
        lease.error_ = "frontend lock is not a regular file owned by the current user";
        return lease;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int lock_errno = errno;
        ::close(fd);
        lease.error_ = (lock_errno == EWOULDBLOCK || lock_errno == EAGAIN)
            ? "another Such frontend is already running"
            : "flock frontend lock failed";
        return lease;
    }
    lease.fd_ = fd;
    lease.acquired_ = true;
#endif
    return lease;
}

void FrontendLease::release() noexcept {
    if (!acquired_) return;
#if defined(_WIN32)
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
#else
    if (fd_ >= 0) { ::flock(fd_, LOCK_UN); ::close(fd_); }
    fd_ = -1;
#endif
    acquired_ = false;
}

} // namespace such::ui

#pragma once

#include <string>

namespace such::ui {

enum class FrontendMode { Gui, Cli, Mcp };

class FrontendLease {
public:
    FrontendLease() = default;
    FrontendLease(const FrontendLease&) = delete;
    FrontendLease& operator=(const FrontendLease&) = delete;
    FrontendLease(FrontendLease&& other) noexcept;
    FrontendLease& operator=(FrontendLease&& other) noexcept;
    ~FrontendLease();

    [[nodiscard]] static FrontendLease try_acquire(FrontendMode mode);
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }
    [[nodiscard]] FrontendMode mode() const noexcept { return mode_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    void release() noexcept;
    FrontendMode mode_ = FrontendMode::Gui;
    bool acquired_ = false;
    std::string error_;
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace such::ui

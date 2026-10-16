#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace such::runtime {

// Shared GUI scheduling policy. Crawls remain owned by the existing isolated
// index worker; this class never touches a live runtime or the index database.
// Timestamps are UTC seconds. Clock injection keeps 24-hour tests deterministic.
class DailyIndexRefresh final {
public:
    using Clock = std::chrono::system_clock;
    static constexpr std::int64_t kRefreshSeconds = 24 * 60 * 60;
    static constexpr std::int64_t kRetrySeconds = 60 * 60;

    explicit DailyIndexRefresh(std::filesystem::path state_file);
    [[nodiscard]] static std::filesystem::path default_state_file();
    [[nodiscard]] bool load(std::string* error = nullptr);
    [[nodiscard]] bool due(std::int64_t now, bool has_roots, bool worker_active) const noexcept;
    [[nodiscard]] bool record_attempt(std::int64_t now, std::string* error = nullptr);
    [[nodiscard]] bool record_success(std::int64_t now, std::string* error = nullptr);
    [[nodiscard]] bool record_failure(std::int64_t now, std::string* error = nullptr);
    [[nodiscard]] std::int64_t last_success() const noexcept { return last_success_; }
    [[nodiscard]] std::int64_t last_attempt() const noexcept { return last_attempt_; }
    [[nodiscard]] static std::int64_t utc_now() noexcept;

private:
    [[nodiscard]] bool persist(std::string* error);
    std::filesystem::path state_file_;
    std::int64_t last_success_ = 0;
    std::int64_t last_attempt_ = 0;
};
} // namespace such::runtime

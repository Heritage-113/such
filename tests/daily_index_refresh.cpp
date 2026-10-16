#include <such/runtime/DailyIndexRefresh.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int check(bool success, int code, const char* name) {
    if (success) return 0;
    std::cerr << "daily index refresh " << name << " failed\n";
    return code;
}
}
int main() {
    using such::runtime::DailyIndexRefresh;
    const auto dir = std::filesystem::temp_directory_path() /
        ("such-daily-" + std::to_string(DailyIndexRefresh::utc_now()) + "-" +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto file = dir / "daily.state";
    std::filesystem::create_directories(dir);
    const auto cleanup = [&]() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    };
    DailyIndexRefresh refresh(file);
    const std::int64_t t = 1'800'000'000;
    std::string error;
    if (int r=check(refresh.load(&error), 1, "initial load")) return r;
    if (int r=check(refresh.due(t, true, false), 2, "fresh due")) return r;
    if (int r=check(!refresh.due(t, false, false) && !refresh.due(t, true, true), 3, "idle/root precondition")) return r;
    if (int r=check(refresh.record_attempt(t, &error), 4, "persist attempt")) return r;
    if (int r=check(!refresh.due(t+1, true, false) && refresh.due(t+DailyIndexRefresh::kRetrySeconds, true, false), 5, "failure throttle")) return r;
    if (int r=check(refresh.record_success(t+DailyIndexRefresh::kRetrySeconds, &error), 6, "success state")) return r;
    const auto success = t + DailyIndexRefresh::kRetrySeconds;
    if (int r=check(!refresh.due(success + DailyIndexRefresh::kRefreshSeconds - 1, true, false), 7, "24h no early recrawl")) return r;
    if (int r=check(refresh.due(success + DailyIndexRefresh::kRefreshSeconds, true, false), 8, "24h due")) return r;
    DailyIndexRefresh reopened(file);
    if (int r=check(reopened.load(&error) && reopened.last_success() == success, 9, "durable restart")) return r;
    if (int r=check(!reopened.due(t, true, false), 10, "clock rollback")) return r;
    if (int r=check(reopened.record_failure(success + DailyIndexRefresh::kRefreshSeconds, &error) &&
                    reopened.last_success() == success, 11, "failure preserves success")) return r;
    if (int r=check(!reopened.due(success + DailyIndexRefresh::kRefreshSeconds + 2, true, false), 12, "retry no storm")) return r;
    if (int r=check(reopened.due(success + DailyIndexRefresh::kRefreshSeconds + DailyIndexRefresh::kRetrySeconds, true, false), 13, "retry resumes")) return r;
    { std::ofstream corrupt(file, std::ios::trunc); corrupt << "INVALID 123 456\n"; }
    DailyIndexRefresh malformed(file);
    if (int r=check(!malformed.load(&error), 14, "corrupt state rejected")) return r;
    { std::ifstream must_preserve(file); std::string magic; must_preserve >> magic;
      if (int r=check(magic=="INVALID",15,"corrupt state not deleted")) return r; }
    cleanup();
    std::cout << "daily index refresh virtual-clock and persistence PASS\n";
    return 0;
}

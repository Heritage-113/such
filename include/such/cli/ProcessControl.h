#pragma once

#include <cstddef>
#include <string>

namespace such::cli {

struct StopResult {
    std::size_t matched = 0;
    std::size_t stopped = 0;
    std::size_t forced = 0;
    std::size_t failed = 0;
    std::string error;
};

// Stops only exact Such process basenames and never the calling process.
[[nodiscard]] StopResult stop_such_processes(bool force) noexcept;

} // namespace such::cli

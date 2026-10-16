#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace such::runtime {

enum class IndexWorkerOperation {
    Reindex,
    AddRoot,
    ReplaceRoots,
};

struct IndexWorkerRequest {
    IndexWorkerOperation operation = IndexWorkerOperation::Reindex;
    std::vector<std::filesystem::path> roots;
    // -1 preserves the saved choice; 0 selects automatic sizing.
    std::int32_t shard_count = -1;
};

enum class IndexWorkerPoll {
    Running,
    Succeeded,
    Failed,
};

struct IndexWorkerTicket {
    std::uintptr_t native_process = 0;
    std::filesystem::path diagnostic_path;

    [[nodiscard]] bool valid() const noexcept { return native_process != 0; }
};

// Launches the installed sibling SuchCLI as a non-interactive indexing worker.
// The GUI must never execute a disk crawl in-process. The worker owns root
// mutation, reindexing, and post-index policy maintenance, then exits.
[[nodiscard]] bool launch_index_worker(
    const IndexWorkerRequest& request,
    std::string* error = nullptr);

// Tracked launch is used by GUI frontends that must reload their read-only
// runtime handle after the external crawler commits a new persistent state.
[[nodiscard]] bool launch_index_worker_tracked(
    const IndexWorkerRequest& request,
    IndexWorkerTicket* ticket,
    std::string* error = nullptr);

[[nodiscard]] IndexWorkerPoll poll_index_worker(
    IndexWorkerTicket* ticket,
    int* exit_code = nullptr) noexcept;

void close_index_worker_ticket(IndexWorkerTicket* ticket) noexcept;

} // namespace such::runtime

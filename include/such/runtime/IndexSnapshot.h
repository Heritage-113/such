#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace such::runtime {

struct IndexSnapshotInfo {
    std::uint32_t format_version = 0;
    std::uint64_t generation = 0;
    std::uint64_t payload_bytes = 0;
    std::uint32_t file_count = 0;
    std::vector<std::filesystem::path> roots;
};

// Resolve the private runtime's persistent state location without inspecting
// its storage format. SUCH_STATE_DIR is authoritative when explicitly set.
// The default Linux location is part of the public snapshot contract.
// Windows snapshot import/export requires SUCH_STATE_DIR until the private ABI
// exposes its resolved state directory directly.
[[nodiscard]] std::filesystem::path index_state_directory(std::string* error = nullptr);

// Snapshot payloads are opaque copies of the private runtime state. Public Such
// records only transport metadata (roots/generation) and never parses TopoDB or
// content-index files.
[[nodiscard]] bool inspect_index_snapshot(
    const std::filesystem::path& snapshot,
    IndexSnapshotInfo* info,
    std::string* error = nullptr);

[[nodiscard]] bool export_index_snapshot(
    const std::filesystem::path& state_directory,
    const std::filesystem::path& snapshot,
    const std::vector<std::filesystem::path>& roots,
    std::uint64_t generation,
    IndexSnapshotInfo* info = nullptr,
    std::string* error = nullptr);

[[nodiscard]] bool import_index_snapshot(
    const std::filesystem::path& snapshot,
    const std::filesystem::path& state_directory,
    IndexSnapshotInfo* info = nullptr,
    std::string* error = nullptr);

} // namespace such::runtime

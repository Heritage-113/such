#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace such::platform::linuxui {

struct IndexRootCandidate {
    std::filesystem::path path;
    std::string label;
    bool network_share = false;
};

struct IndexRootDiscoveryConfig {
    std::filesystem::path mountinfo_path = "/proc/self/mountinfo";
    std::optional<std::filesystem::path> home_override;
    std::optional<std::filesystem::path> gvfs_root_override;
};

// Linux root access is intentionally evaluated with the current process
// credentials. Such never elevates or bypasses Samba/CIFS permissions.
[[nodiscard]] bool index_root_accessible(const std::filesystem::path& path);

// Enumerate user-searchable Linux roots without requiring elevated privileges.
// HOME is offered first when valid. Ordinary mounted filesystems and authenticated
// CIFS/SMB3 mounts are discovered from mountinfo. Per-user GVfs smb:// mounts are
// surfaced from $XDG_RUNTIME_DIR/gvfs even though /run is otherwise hidden.
[[nodiscard]] std::vector<IndexRootCandidate> discover_index_root_candidates();

// Deterministic discovery seam used by Linux contract tests. Production callers
// use discover_index_root_candidates() and never provide synthetic mount state.
[[nodiscard]] std::vector<IndexRootCandidate> discover_index_root_candidates(
    const IndexRootDiscoveryConfig& config);

} // namespace such::platform::linuxui

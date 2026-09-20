#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace such::platform::linuxui {

struct IndexRootCandidate {
    std::filesystem::path path;
    std::string label;
};

// Enumerate user-searchable Linux roots without requiring elevated privileges.
// HOME is always offered first when valid; mounted volumes are discovered from
// /proc/self/mountinfo. Kernel/pseudo filesystems and the filesystem root are
// intentionally omitted from the picker.
[[nodiscard]] std::vector<IndexRootCandidate> discover_index_root_candidates();

} // namespace such::platform::linuxui

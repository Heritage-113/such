#include "LinuxIndexRoots.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <pwd.h>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace such::platform::linuxui {
namespace {

std::optional<std::filesystem::path> user_home() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }
    if (const passwd* pw = ::getpwuid(::getuid()); pw != nullptr && pw->pw_dir != nullptr && *pw->pw_dir != '\0') {
        return std::filesystem::path(pw->pw_dir);
    }
    return std::nullopt;
}

std::string decode_mount_field(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 3 < value.size() &&
            value[i + 1] >= '0' && value[i + 1] <= '7' &&
            value[i + 2] >= '0' && value[i + 2] <= '7' &&
            value[i + 3] >= '0' && value[i + 3] <= '7') {
            const unsigned code = static_cast<unsigned>(value[i + 1] - '0') * 64u +
                                  static_cast<unsigned>(value[i + 2] - '0') * 8u +
                                  static_cast<unsigned>(value[i + 3] - '0');
            out.push_back(static_cast<char>(code));
            i += 3;
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

bool pseudo_filesystem(std::string_view type) {
    static constexpr std::array<std::string_view, 20> blocked{
        "proc", "sysfs", "devtmpfs", "devpts", "securityfs", "cgroup", "cgroup2",
        "pstore", "debugfs", "tracefs", "configfs", "fusectl", "mqueue", "hugetlbfs",
        "rpc_pipefs", "binfmt_misc", "autofs", "selinuxfs", "efivarfs", "bpf"};
    return std::find(blocked.begin(), blocked.end(), type) != blocked.end();
}

bool system_mount_path(const std::filesystem::path& path) {
    const std::string value = path.lexically_normal().string();
    if (value.empty() || value == "/") return true;
    if (value == "/proc" || value.starts_with("/proc/")) return true;
    if (value == "/sys" || value.starts_with("/sys/")) return true;
    if (value == "/dev" || value.starts_with("/dev/")) return true;
    if (value == "/boot" || value.starts_with("/boot/")) return true;
    if (value == "/snap" || value.starts_with("/snap/")) return true;
    if (value == "/var/lib/docker" || value.starts_with("/var/lib/docker/")) return true;
    if (value == "/var/lib/containers" || value.starts_with("/var/lib/containers/")) return true;
    // /run is mostly transient system state; desktop-mounted removable media is
    // the exception and is intentionally visible.
    if ((value == "/run" || value.starts_with("/run/")) &&
        !(value == "/run/media" || value.starts_with("/run/media/"))) return true;
    return false;
}

bool same_directory(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
    std::error_code ec;
    if (std::filesystem::equivalent(lhs, rhs, ec) && !ec) return true;
    ec.clear();
    const auto left = std::filesystem::weakly_canonical(lhs, ec);
    if (ec) return lhs.lexically_normal() == rhs.lexically_normal();
    ec.clear();
    const auto right = std::filesystem::weakly_canonical(rhs, ec);
    if (ec) return lhs.lexically_normal() == rhs.lexically_normal();
    return left == right;
}

void add_candidate(std::vector<IndexRootCandidate>& out, const std::filesystem::path& path, std::string label) {
    if (path.empty() || system_mount_path(path)) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec) || ec) return;
    if (std::any_of(out.begin(), out.end(), [&](const auto& item) { return same_directory(item.path, path); })) return;
    if (label.empty()) {
        label = path.filename().string();
        if (label.empty()) label = path.string();
    }
    out.push_back({path, std::move(label)});
}

} // namespace

std::vector<IndexRootCandidate> discover_index_root_candidates() {
    std::vector<IndexRootCandidate> out;
    const auto home = user_home();
    if (home.has_value()) add_candidate(out, *home, "Home");

    std::ifstream mountinfo("/proc/self/mountinfo");
    std::string line;
    while (std::getline(mountinfo, line)) {
        std::istringstream fields(line);
        std::vector<std::string> tokens;
        std::string token;
        while (fields >> token) tokens.push_back(token);
        const auto dash = std::find(tokens.begin(), tokens.end(), "-");
        if (tokens.size() < 7 || dash == tokens.end()) continue;
        const std::size_t dash_index = static_cast<std::size_t>(std::distance(tokens.begin(), dash));
        if (dash_index + 1 >= tokens.size() || dash_index <= 4) continue;
        const std::string_view fs_type(tokens[dash_index + 1]);
        if (pseudo_filesystem(fs_type)) continue;

        const std::filesystem::path mount_path(decode_mount_field(tokens[4]));
        if (system_mount_path(mount_path)) continue;
        std::string label = mount_path.filename().string();
        if (home.has_value() && same_directory(mount_path, *home)) label = "Home";
        add_candidate(out, mount_path, std::move(label));
    }

    if (out.size() > 1) {
        const std::size_t start = (home.has_value() && same_directory(out.front().path, *home)) ? 1u : 0u;
        std::sort(out.begin() + static_cast<std::ptrdiff_t>(start), out.end(), [](const auto& a, const auto& b) {
            return a.path.string() < b.path.string();
        });
    }
    return out;
}

} // namespace such::platform::linuxui

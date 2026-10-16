#include "LinuxIndexRoots.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <dirent.h>
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

std::optional<std::filesystem::path> user_gvfs_root() {
    if (const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR"); runtime_dir != nullptr && *runtime_dir != '\0') {
        return std::filesystem::path(runtime_dir) / "gvfs";
    }
    return std::filesystem::path("/run/user") / std::to_string(::getuid()) / "gvfs";
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

bool samba_filesystem(std::string_view type) {
    return type == "cifs" || type == "smb3" || type == "smbfs" || type == "fuse.smbnetfs";
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
    // /run is mostly transient system state. Per-user GVfs shares are added by a
    // dedicated path below rather than exposing the whole runtime directory.
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

std::string trim_smb_source(std::string source) {
    source = decode_mount_field(source);
    while (source.starts_with("//")) source.erase(0, 2);
    return source;
}

std::string gvfs_smb_label(std::string_view name) {
    constexpr std::string_view prefix = "smb-share:";
    if (!name.starts_with(prefix)) return {};
    std::string server;
    std::string share;
    std::string_view fields = name.substr(prefix.size());
    while (!fields.empty()) {
        const auto comma = fields.find(',');
        const auto field = fields.substr(0, comma);
        const auto equal = field.find('=');
        if (equal != std::string_view::npos) {
            const auto key = field.substr(0, equal);
            const auto value = field.substr(equal + 1);
            if (key == "server") server.assign(value);
            else if (key == "share") share.assign(value);
        }
        if (comma == std::string_view::npos) break;
        fields.remove_prefix(comma + 1);
    }
    if (!server.empty() && !share.empty()) return "SMB " + server + "/" + share;
    if (!server.empty()) return "SMB " + server;
    return "SMB " + std::string(name.substr(prefix.size()));
}

void add_candidate(std::vector<IndexRootCandidate>& out,
                   const std::filesystem::path& path,
                   std::string label,
                   bool network_share,
                   bool allow_runtime_path = false) {
    if (path.empty() || (!allow_runtime_path && system_mount_path(path))) return;
    if (!index_root_accessible(path)) return;
    if (std::any_of(out.begin(), out.end(), [&](const auto& item) { return same_directory(item.path, path); })) return;
    if (label.empty()) {
        label = path.filename().string();
        if (label.empty()) label = path.string();
    }
    out.push_back({path, std::move(label), network_share});
}

void add_gvfs_samba_candidates(std::vector<IndexRootCandidate>& out,
                               const std::optional<std::filesystem::path>& gvfs_root) {
    if (!gvfs_root.has_value()) return;
    std::error_code ec;
    if (!std::filesystem::is_directory(*gvfs_root, ec) || ec) return;

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::directory_iterator it(*gvfs_root, options, ec), end;
    while (!ec && it != end) {
        const std::string name = it->path().filename().string();
        if (name.starts_with("smb-share:")) {
            add_candidate(out, it->path(), gvfs_smb_label(name), true, true);
        }
        it.increment(ec);
        if (ec == std::errc::permission_denied) ec.clear();
    }
}

std::vector<IndexRootCandidate> discover_with_config(const IndexRootDiscoveryConfig& config) {
    std::vector<IndexRootCandidate> out;
    const auto home = config.home_override.has_value() ? config.home_override : user_home();
    const auto gvfs_root = config.gvfs_root_override.has_value() ? config.gvfs_root_override : user_gvfs_root();
    if (home.has_value()) add_candidate(out, *home, "Home", false);

    std::ifstream mountinfo(config.mountinfo_path);
    std::string line;
    while (std::getline(mountinfo, line)) {
        std::istringstream fields(line);
        std::vector<std::string> tokens;
        std::string token;
        while (fields >> token) tokens.push_back(token);
        const auto dash = std::find(tokens.begin(), tokens.end(), "-");
        if (tokens.size() < 7 || dash == tokens.end()) continue;
        const std::size_t dash_index = static_cast<std::size_t>(std::distance(tokens.begin(), dash));
        if (dash_index + 2 >= tokens.size() || dash_index <= 4) continue;
        const std::string_view fs_type(tokens[dash_index + 1]);
        if (pseudo_filesystem(fs_type)) continue;

        const std::filesystem::path mount_path(decode_mount_field(tokens[4]));
        if (system_mount_path(mount_path)) continue;

        const bool network_share = samba_filesystem(fs_type);
        std::string label;
        if (network_share) {
            const auto source = trim_smb_source(tokens[dash_index + 2]);
            label = source.empty() ? "SMB " + mount_path.filename().string() : "SMB " + source;
        } else {
            label = mount_path.filename().string();
        }
        if (home.has_value() && same_directory(mount_path, *home)) label = "Home";
        add_candidate(out, mount_path, std::move(label), network_share);
    }

    // GNOME/GVfs mounts smb:// shares below /run/user/<uid>/gvfs. mountinfo only
    // exposes the gvfsd-fuse container mount, not each SMB child as a mountpoint.
    // Enumerate only immediate smb-share:* children and keep normal /run hidden.
    add_gvfs_samba_candidates(out, gvfs_root);

    if (out.size() > 1) {
        const std::size_t start = (home.has_value() && same_directory(out.front().path, *home)) ? 1u : 0u;
        std::sort(out.begin() + static_cast<std::ptrdiff_t>(start), out.end(), [](const auto& a, const auto& b) {
            if (a.network_share != b.network_share) return a.network_share > b.network_share;
            return a.path.string() < b.path.string();
        });
    }
    return out;
}

} // namespace

bool index_root_accessible(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec) || ec) return false;
    DIR* directory = ::opendir(path.c_str());
    if (directory == nullptr) return false;
    ::closedir(directory);
    return true;
}

std::vector<IndexRootCandidate> discover_index_root_candidates() {
    return discover_with_config(IndexRootDiscoveryConfig{});
}

std::vector<IndexRootCandidate> discover_index_root_candidates(const IndexRootDiscoveryConfig& config) {
    return discover_with_config(config);
}

} // namespace such::platform::linuxui

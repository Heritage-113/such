#include "../platform/linux/LinuxIndexRoots.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int fail(int code, const char* message) {
    std::cerr << "linux index-root contract FAIL: " << message << '\n';
    return code;
}

const such::platform::linuxui::IndexRootCandidate* find_path(
    const std::vector<such::platform::linuxui::IndexRootCandidate>& items,
    const std::filesystem::path& path) {
    for (const auto& item : items) {
        if (item.path.lexically_normal() == path.lexically_normal()) return &item;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && argv != nullptr) {
        const std::filesystem::path probe_path(argv[2]);
        if (std::string(argv[1]) == "--expect-accessible") {
            return such::platform::linuxui::index_root_accessible(probe_path) ? 0 : 20;
        }
        if (std::string(argv[1]) == "--expect-inaccessible") {
            return such::platform::linuxui::index_root_accessible(probe_path) ? 21 : 0;
        }
    }

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("such-linux-roots-" + std::to_string(static_cast<long long>(::getpid())));
    const fs::path home = root / "home";
    const fs::path local = root / "local";
    const fs::path cifs = root / "nas-projects";
    const fs::path missing = root / "missing-share";
    const fs::path gvfs = root / "gvfs";
    const fs::path gvfs_share = gvfs / "smb-share:server=FILES,share=Design";
    const fs::path mountinfo = root / "mountinfo";
    const fs::path regular_file = root / "file.txt";

    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(home, ec);
    fs::create_directories(local, ec);
    fs::create_directories(cifs, ec);
    fs::create_directories(gvfs_share, ec);
    if (ec) return fail(2, "fixture directories");
    std::ofstream(regular_file) << "x";

    {
        std::ofstream out(mountinfo);
        out << "31 1 0:31 / " << local.string()
            << " rw,relatime - ext4 /dev/test rw\n";
        out << "32 1 0:32 / " << cifs.string()
            << " rw,relatime - cifs //NAS/Projects rw,vers=3.1.1\n";
        out << "33 1 0:33 / " << missing.string()
            << " rw,relatime - cifs //NAS/Missing rw,vers=3.1.1\n";
        out << "34 1 0:34 / /proc rw,nosuid - proc proc rw\n";
    }

    such::platform::linuxui::IndexRootDiscoveryConfig config;
    config.mountinfo_path = mountinfo;
    config.home_override = home;
    config.gvfs_root_override = gvfs;
    const auto items = such::platform::linuxui::discover_index_root_candidates(config);

    if (items.empty() || items.front().path != home || items.front().label != "Home") {
        fs::remove_all(root, ec);
        return fail(3, "HOME ordering");
    }

    const auto* cifs_item = find_path(items, cifs);
    if (cifs_item == nullptr || !cifs_item->network_share || cifs_item->label != "SMB NAS/Projects") {
        fs::remove_all(root, ec);
        return fail(4, "CIFS discovery/label");
    }

    const auto* gvfs_item = find_path(items, gvfs_share);
    if (gvfs_item == nullptr || !gvfs_item->network_share || gvfs_item->label != "SMB FILES/Design") {
        fs::remove_all(root, ec);
        return fail(5, "GVfs SMB discovery/label");
    }

    const auto* local_item = find_path(items, local);
    if (local_item == nullptr || local_item->network_share) {
        fs::remove_all(root, ec);
        return fail(6, "local mount discovery");
    }

    if (find_path(items, missing) != nullptr || find_path(items, "/proc") != nullptr) {
        fs::remove_all(root, ec);
        return fail(7, "inaccessible/system mount exclusion");
    }

    if (!such::platform::linuxui::index_root_accessible(cifs) ||
        such::platform::linuxui::index_root_accessible(regular_file) ||
        such::platform::linuxui::index_root_accessible(missing)) {
        fs::remove_all(root, ec);
        return fail(8, "current-user root accessibility");
    }

    fs::remove_all(root, ec);
    std::cout << "linux index-root contract PASS\n";
    return 0;
}

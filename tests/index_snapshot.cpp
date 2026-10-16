#include <such/runtime/IndexSnapshot.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::filesystem::path temp_root() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("such-index-snapshot-test-" + std::to_string(static_cast<unsigned long long>(stamp)));
}

bool write_file(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
    return static_cast<bool>(out);
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const auto root = temp_root();
    const auto state = root / "state";
    const auto imported = root / "imported";
    const auto search_root = root / "files";
    const auto snapshot = root / "portable.sidx";
    std::error_code ec;
    std::filesystem::create_directories(search_root, ec);
    if (ec) return 1;

    if (!write_file(state / "roots.txt", search_root.string() + "\n") ||
        !write_file(state / "search.topodb64", "opaque-index-payload") ||
        !write_file(state / "content" / "blocks.bin", "opaque-content-payload") ||
        !write_file(state / "search.topodb64.lock", "must-not-travel")) {
        std::cerr << "fixture write failed\n";
        return 2;
    }

    const std::vector<std::filesystem::path> roots{search_root};
    such::runtime::IndexSnapshotInfo exported;
    std::string error;
    if (!such::runtime::export_index_snapshot(state, snapshot, roots, 42u, &exported, &error)) {
        std::cerr << "export failed: " << error << '\n';
        return 3;
    }
    if (exported.generation != 42u || exported.roots != roots || exported.file_count != 3u) {
        std::cerr << "export metadata mismatch\n";
        return 4;
    }

    such::runtime::IndexSnapshotInfo inspected;
    if (!such::runtime::inspect_index_snapshot(snapshot, &inspected, &error)) {
        std::cerr << "inspect failed: " << error << '\n';
        return 5;
    }
    if (inspected.generation != 42u || inspected.roots != roots || inspected.file_count != 3u) {
        std::cerr << "inspect metadata mismatch\n";
        return 6;
    }

    if (!write_file(imported / "old.txt", "old-state")) return 7;
    such::runtime::IndexSnapshotInfo restored;
    if (!such::runtime::import_index_snapshot(snapshot, imported, &restored, &error)) {
        std::cerr << "import failed: " << error << '\n';
        return 8;
    }
    if (read_file(imported / "search.topodb64") != "opaque-index-payload" ||
        read_file(imported / "content" / "blocks.bin") != "opaque-content-payload" ||
        std::filesystem::exists(imported / "search.topodb64.lock") ||
        std::filesystem::exists(imported / "old.txt")) {
        std::cerr << "imported payload mismatch\n";
        return 9;
    }

    // Corruption must be rejected before state replacement.
    const auto corrupt = root / "corrupt.sidx";
    std::filesystem::copy_file(snapshot, corrupt, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return 10;
    {
        std::fstream io(corrupt, std::ios::binary | std::ios::in | std::ios::out);
        io.seekp(-1, std::ios::end);
        char byte = 0;
        io.read(&byte, 1);
        io.clear();
        io.seekp(-1, std::ios::end);
        byte ^= 0x5a;
        io.write(&byte, 1);
    }
    if (such::runtime::inspect_index_snapshot(corrupt, nullptr, &error)) {
        std::cerr << "corrupt snapshot unexpectedly accepted\n";
        return 11;
    }

    std::filesystem::remove_all(search_root, ec);
    const auto missing_root_import = root / "missing-root-import";
    if (such::runtime::import_index_snapshot(snapshot, missing_root_import, nullptr, &error)) {
        std::cerr << "snapshot with unavailable root unexpectedly imported\n";
        return 12;
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "index snapshot contract PASS\n";
    return 0;
}

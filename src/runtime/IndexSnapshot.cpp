#include <such/runtime/IndexSnapshot.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

namespace such::runtime {
namespace {

constexpr std::array<char, 8> kMagic{{'S','U','C','H','I','D','X','1'}};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kMaxStringBytes = 1024u * 1024u;
constexpr std::uint32_t kMaxEntries = 1'000'000u;
constexpr std::size_t kCopyBufferBytes = 1024u * 1024u;

void set_error(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
}

std::optional<std::string> env_value(const char* name) {
#if defined(_WIN32)
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) return std::nullopt;
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    if (const char* value = std::getenv(name)) return std::string(value);
    return std::nullopt;
#endif
}

std::string path_utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::filesystem::path utf8_path(const std::string& value) {
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size())));
}

bool write_u32(std::ostream& out, std::uint32_t value) {
    std::array<unsigned char, 4> b{{
        static_cast<unsigned char>(value & 0xffu),
        static_cast<unsigned char>((value >> 8u) & 0xffu),
        static_cast<unsigned char>((value >> 16u) & 0xffu),
        static_cast<unsigned char>((value >> 24u) & 0xffu)}};
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(out);
}

bool write_u64(std::ostream& out, std::uint64_t value) {
    std::array<unsigned char, 8> b{};
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<unsigned char>((value >> (i * 8u)) & 0xffu);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(out);
}

bool read_u32(std::istream& in, std::uint32_t& value) {
    std::array<unsigned char, 4> b{};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!in) return false;
    value = static_cast<std::uint32_t>(b[0]) |
            (static_cast<std::uint32_t>(b[1]) << 8u) |
            (static_cast<std::uint32_t>(b[2]) << 16u) |
            (static_cast<std::uint32_t>(b[3]) << 24u);
    return true;
}

bool read_u64(std::istream& in, std::uint64_t& value) {
    std::array<unsigned char, 8> b{};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!in) return false;
    value = 0;
    for (std::size_t i = 0; i < b.size(); ++i) value |= static_cast<std::uint64_t>(b[i]) << (i * 8u);
    return true;
}

bool write_string(std::ostream& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    if (!write_u32(out, static_cast<std::uint32_t>(value.size()))) return false;
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

bool read_string(std::istream& in, std::string& value) {
    std::uint32_t size = 0;
    if (!read_u32(in, size) || size > kMaxStringBytes) return false;
    value.assign(size, '\0');
    if (size != 0u) in.read(value.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(in);
}

std::uint32_t crc32_update(std::uint32_t crc, const unsigned char* data, std::size_t size) {
    crc = ~crc;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

bool excluded_state_file(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    if (name.empty()) return true;
    if (path.extension() == ".lock") return true;
    if (path.extension() == ".tmp") return true;
    if (name.size() >= 4u && name.substr(name.size() - 4u) == ".new") return true;
    return false;
}

struct StateEntry {
    std::filesystem::path absolute;
    std::string relative_utf8;
    std::uint64_t size = 0;
};

class ScopedPathCleanup {
public:
    explicit ScopedPathCleanup(std::filesystem::path path) : path_(std::move(path)) {}
    ScopedPathCleanup(const ScopedPathCleanup&) = delete;
    ScopedPathCleanup& operator=(const ScopedPathCleanup&) = delete;
    ~ScopedPathCleanup() {
        if (!active_) return;
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    void release() noexcept { active_ = false; }

private:
    std::filesystem::path path_;
    bool active_ = true;
};

bool collect_state_entries(
    const std::filesystem::path& state_directory,
    std::vector<StateEntry>& entries,
    std::uint64_t& total_bytes,
    std::string* error) {
    std::error_code ec;
    if (!std::filesystem::is_directory(state_directory, ec)) {
        set_error(error, "index state directory is missing: " + state_directory.string());
        return false;
    }
    for (std::filesystem::recursive_directory_iterator it(
             state_directory,
             std::filesystem::directory_options::skip_permission_denied,
             ec), end;
         it != end;
         it.increment(ec)) {
        if (ec) {
            set_error(error, "could not enumerate index state: " + ec.message());
            return false;
        }
        const auto& p = it->path();
        const auto status = it->symlink_status(ec);
        if (ec) {
            set_error(error, "could not inspect index state entry: " + p.string());
            return false;
        }
        if (std::filesystem::is_symlink(status)) {
            set_error(error, "snapshot refuses symlinks in index state: " + p.string());
            return false;
        }
        if (!std::filesystem::is_regular_file(status) || excluded_state_file(p)) continue;
        const auto rel = std::filesystem::relative(p, state_directory, ec);
        if (ec || rel.empty()) {
            set_error(error, "could not relativize index state entry: " + p.string());
            return false;
        }
        const auto size = std::filesystem::file_size(p, ec);
        if (ec) {
            set_error(error, "could not stat index state entry: " + p.string());
            return false;
        }
        if (entries.size() >= kMaxEntries) {
            set_error(error, "snapshot contains too many state files");
            return false;
        }
        entries.push_back(StateEntry{p, path_utf8(rel), size});
        if (std::numeric_limits<std::uint64_t>::max() - total_bytes < size) {
            set_error(error, "snapshot payload size overflow");
            return false;
        }
        total_bytes += size;
    }
    return true;
}

bool valid_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& component : path) {
        if (component == "." || component == "..") return false;
    }
    return true;
}

bool read_header(std::istream& in, IndexSnapshotInfo& info, std::uint32_t& entry_count, std::string* error) {
    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) {
        set_error(error, "not a Such index snapshot");
        return false;
    }
    if (!read_u32(in, info.format_version) || info.format_version != kFormatVersion) {
        set_error(error, "unsupported Such index snapshot format");
        return false;
    }
    std::uint32_t root_count = 0;
    if (!read_u32(in, root_count) || root_count > 4096u || !read_u64(in, info.generation) || !read_u32(in, entry_count) || entry_count > kMaxEntries) {
        set_error(error, "corrupt Such index snapshot header");
        return false;
    }
    info.file_count = entry_count;
    info.roots.clear();
    info.roots.reserve(root_count);
    for (std::uint32_t i = 0; i < root_count; ++i) {
        std::string root;
        if (!read_string(in, root) || root.empty()) {
            set_error(error, "corrupt Such index snapshot root table");
            return false;
        }
        info.roots.push_back(utf8_path(root));
    }
    return true;
}

std::filesystem::path unique_sibling(const std::filesystem::path& state_directory, const char* suffix) {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return state_directory.parent_path() /
        (state_directory.filename().string() + suffix + std::to_string(static_cast<unsigned long long>(now)));
}

} // namespace

std::filesystem::path index_state_directory(std::string* error) {
    if (const auto explicit_dir = env_value("SUCH_STATE_DIR")) {
        if (!explicit_dir->empty()) return std::filesystem::absolute(std::filesystem::path(*explicit_dir)).lexically_normal();
    }
#if defined(_WIN32)
    set_error(error, "Windows snapshot import/export currently requires SUCH_STATE_DIR to be set explicitly");
    return {};
#else
    std::filesystem::path data_home;
    if (const auto xdg = env_value("XDG_DATA_HOME")) {
        if (!xdg->empty()) data_home = std::filesystem::path(*xdg);
    }
    if (data_home.empty()) {
        const auto home = env_value("HOME");
        if (!home || home->empty()) {
            set_error(error, "HOME/XDG_DATA_HOME is unavailable; cannot resolve Such index state");
            return {};
        }
        data_home = std::filesystem::path(*home) / ".local" / "share";
    }
    return std::filesystem::absolute(data_home / "such" / "index").lexically_normal();
#endif
}

bool inspect_index_snapshot(const std::filesystem::path& snapshot, IndexSnapshotInfo* info, std::string* error) {
    std::ifstream in(snapshot, std::ios::binary);
    if (!in) {
        set_error(error, "could not open snapshot: " + snapshot.string());
        return false;
    }
    IndexSnapshotInfo parsed;
    std::uint32_t entries = 0;
    if (!read_header(in, parsed, entries, error)) return false;
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < entries; ++i) {
        std::string rel;
        std::uint64_t size = 0;
        std::uint32_t expected_crc = 0;
        if (!read_string(in, rel) || !read_u64(in, size) || !read_u32(in, expected_crc)) {
            set_error(error, "truncated Such index snapshot entry header");
            return false;
        }
        const auto rel_path = utf8_path(rel);
        if (!valid_relative_path(rel_path)) {
            set_error(error, "unsafe path in Such index snapshot: " + rel);
            return false;
        }
        std::vector<unsigned char> buffer(kCopyBufferBytes);
        std::uint64_t remaining = size;
        std::uint32_t crc = 0;
        while (remaining != 0u) {
            const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
            in.read(reinterpret_cast<char*>(buffer.data()), chunk);
            if (!in) {
                set_error(error, "truncated Such index snapshot payload");
                return false;
            }
            crc = crc32_update(crc, buffer.data(), static_cast<std::size_t>(chunk));
            remaining -= static_cast<std::uint64_t>(chunk);
        }
        if (crc != expected_crc) {
            set_error(error, "Such index snapshot CRC mismatch: " + rel);
            return false;
        }
        if (std::numeric_limits<std::uint64_t>::max() - total < size) {
            set_error(error, "Such index snapshot payload size overflow");
            return false;
        }
        total += size;
    }
    const auto tail = in.peek();
    if (tail != std::char_traits<char>::eof()) {
        set_error(error, "unexpected trailing bytes in Such index snapshot");
        return false;
    }
    parsed.payload_bytes = total;
    if (info != nullptr) *info = std::move(parsed);
    return true;
}

bool export_index_snapshot(
    const std::filesystem::path& state_directory,
    const std::filesystem::path& snapshot,
    const std::vector<std::filesystem::path>& roots,
    std::uint64_t generation,
    IndexSnapshotInfo* info,
    std::string* error) {
    std::vector<StateEntry> entries;
    std::uint64_t total_bytes = 0;
    if (!collect_state_entries(state_directory, entries, total_bytes, error)) return false;
    if (roots.size() > 4096u) {
        set_error(error, "too many search roots for snapshot");
        return false;
    }

    std::error_code ec;
    if (!snapshot.parent_path().empty()) std::filesystem::create_directories(snapshot.parent_path(), ec);
    if (ec) {
        set_error(error, "could not create snapshot output directory: " + ec.message());
        return false;
    }
    const std::filesystem::path temp = snapshot.string() + ".tmp";
    ScopedPathCleanup temp_cleanup(temp);
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        set_error(error, "could not create snapshot: " + temp.string());
        return false;
    }
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    if (!write_u32(out, kFormatVersion) ||
        !write_u32(out, static_cast<std::uint32_t>(roots.size())) ||
        !write_u64(out, generation) ||
        !write_u32(out, static_cast<std::uint32_t>(entries.size()))) {
        set_error(error, "could not write snapshot header");
        return false;
    }
    for (const auto& root : roots) {
        if (!write_string(out, path_utf8(root))) {
            set_error(error, "could not write snapshot root table");
            return false;
        }
    }
    std::vector<unsigned char> buffer(kCopyBufferBytes);
    for (const auto& entry : entries) {
        std::ifstream input(entry.absolute, std::ios::binary);
        if (!input) {
            set_error(error, "could not read index state file: " + entry.absolute.string());
            return false;
        }
        std::uint32_t crc = 0;
        while (input) {
            input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto got = input.gcount();
            if (got > 0) crc = crc32_update(crc, buffer.data(), static_cast<std::size_t>(got));
        }
        input.clear();
        input.seekg(0, std::ios::beg);
        if (!write_string(out, entry.relative_utf8) || !write_u64(out, entry.size) || !write_u32(out, crc)) {
            set_error(error, "could not write snapshot entry header");
            return false;
        }
        std::uint64_t remaining = entry.size;
        while (remaining != 0u) {
            const auto want = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
            input.read(reinterpret_cast<char*>(buffer.data()), want);
            if (input.gcount() != want) {
                set_error(error, "index state file changed while snapshot was being exported: " + entry.absolute.string());
                return false;
            }
            out.write(reinterpret_cast<const char*>(buffer.data()), want);
            if (!out) {
                set_error(error, "could not write snapshot payload");
                return false;
            }
            remaining -= static_cast<std::uint64_t>(want);
        }
    }
    out.flush();
    out.close();
    if (!out) {
        set_error(error, "could not finalize snapshot");
        return false;
    }
    std::filesystem::remove(snapshot, ec);
    ec.clear();
    std::filesystem::rename(temp, snapshot, ec);
    if (ec) {
        set_error(error, "could not publish snapshot: " + ec.message());
        return false;
    }
    temp_cleanup.release();
    if (info != nullptr) {
        info->format_version = kFormatVersion;
        info->generation = generation;
        info->payload_bytes = total_bytes;
        info->file_count = static_cast<std::uint32_t>(entries.size());
        info->roots = roots;
    }
    return true;
}

bool import_index_snapshot(
    const std::filesystem::path& snapshot,
    const std::filesystem::path& state_directory,
    IndexSnapshotInfo* info,
    std::string* error) {
    std::ifstream in(snapshot, std::ios::binary);
    if (!in) {
        set_error(error, "could not open snapshot: " + snapshot.string());
        return false;
    }
    IndexSnapshotInfo parsed;
    std::uint32_t entries = 0;
    if (!read_header(in, parsed, entries, error)) return false;
    for (const auto& root : parsed.roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) {
            set_error(error, "snapshot root is not available at the original path: " + root.string());
            return false;
        }
    }

    const auto temp_dir = unique_sibling(state_directory, ".snapshot-import-");
    const auto backup_dir = unique_sibling(state_directory, ".snapshot-backup-");
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    ec.clear();
    std::filesystem::create_directories(temp_dir, ec);
    if (ec) {
        set_error(error, "could not create snapshot import staging directory: " + ec.message());
        return false;
    }

    std::vector<unsigned char> buffer(kCopyBufferBytes);
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < entries; ++i) {
        std::string rel;
        std::uint64_t size = 0;
        std::uint32_t expected_crc = 0;
        if (!read_string(in, rel) || !read_u64(in, size) || !read_u32(in, expected_crc)) {
            std::filesystem::remove_all(temp_dir, ec);
            set_error(error, "truncated Such index snapshot entry header");
            return false;
        }
        const auto rel_path = utf8_path(rel);
        if (!valid_relative_path(rel_path)) {
            std::filesystem::remove_all(temp_dir, ec);
            set_error(error, "unsafe path in Such index snapshot: " + rel);
            return false;
        }
        const auto output_path = temp_dir / rel_path;
        std::filesystem::create_directories(output_path.parent_path(), ec);
        if (ec) {
            std::filesystem::remove_all(temp_dir, ec);
            set_error(error, "could not create snapshot state directory: " + ec.message());
            return false;
        }
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::filesystem::remove_all(temp_dir, ec);
            set_error(error, "could not create imported state file: " + output_path.string());
            return false;
        }
        std::uint64_t remaining = size;
        std::uint32_t crc = 0;
        while (remaining != 0u) {
            const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
            in.read(reinterpret_cast<char*>(buffer.data()), chunk);
            if (!in) {
                std::filesystem::remove_all(temp_dir, ec);
                set_error(error, "truncated Such index snapshot payload");
                return false;
            }
            crc = crc32_update(crc, buffer.data(), static_cast<std::size_t>(chunk));
            out.write(reinterpret_cast<const char*>(buffer.data()), chunk);
            if (!out) {
                std::filesystem::remove_all(temp_dir, ec);
                set_error(error, "could not write imported state file: " + output_path.string());
                return false;
            }
            remaining -= static_cast<std::uint64_t>(chunk);
        }
        out.close();
        if (crc != expected_crc) {
            std::filesystem::remove_all(temp_dir, ec);
            set_error(error, "Such index snapshot CRC mismatch: " + rel);
            return false;
        }
        if (std::numeric_limits<std::uint64_t>::max() - total < size) {
            std::filesystem::remove_all(temp_dir, ec);
            set_error(error, "Such index snapshot payload size overflow");
            return false;
        }
        total += size;
    }
    if (in.peek() != std::char_traits<char>::eof()) {
        std::filesystem::remove_all(temp_dir, ec);
        set_error(error, "unexpected trailing bytes in Such index snapshot");
        return false;
    }

    const bool had_previous = std::filesystem::exists(state_directory, ec);
    if (ec) {
        std::filesystem::remove_all(temp_dir, ec);
        set_error(error, "could not inspect current index state: " + ec.message());
        return false;
    }
    if (had_previous) {
        std::filesystem::remove_all(backup_dir, ec);
        ec.clear();
        std::filesystem::rename(state_directory, backup_dir, ec);
        if (ec) {
            std::filesystem::remove_all(temp_dir, ec);
            set_error(error, "could not move current index state aside: " + ec.message());
            return false;
        }
    }
    std::filesystem::rename(temp_dir, state_directory, ec);
    if (ec) {
        std::error_code rollback_ec;
        if (had_previous) std::filesystem::rename(backup_dir, state_directory, rollback_ec);
        std::filesystem::remove_all(temp_dir, rollback_ec);
        set_error(error, "could not activate imported index state: " + ec.message());
        return false;
    }
    if (had_previous) std::filesystem::remove_all(backup_dir, ec);

    parsed.payload_bytes = total;
    if (info != nullptr) *info = std::move(parsed);
    return true;
}

} // namespace such::runtime

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace such::sdk {

enum class Dialect : std::uint8_t {
    Native,
    Windows,
    UnixLike,
};

struct SearchResult {
    std::uint64_t file_id = 0;
    std::string filename;
    std::string path;
    std::string extension;
    bool pinned = false;
    bool indexed = true;
    bool authorized = true;
    std::int64_t modified_unix_seconds = 0;
    std::uint64_t size_bytes = 0;
};

struct Status {
    bool indexing = false;
    std::size_t indexed_files = 0;
    std::uint64_t generation = 0;
    std::vector<std::string> roots;
    std::string last_error;
};

// Stable C++ integration surface for embedding Such search into another native
// application. The SDK wraps the public runtime client; it does not expose the
// private search/storage implementation.
class Client final {
public:
    explicit Client(std::filesystem::path state_directory = {});
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    [[nodiscard]] bool open(std::string* error = nullptr);
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] std::string last_error() const;

    [[nodiscard]] std::vector<SearchResult> search(
        std::string_view query,
        Dialect dialect = Dialect::Native,
        std::size_t max_results = 0,
        std::string* error = nullptr) const;

    [[nodiscard]] bool add_root(const std::filesystem::path& root, std::string* error = nullptr);
    [[nodiscard]] bool remove_root(const std::filesystem::path& root, std::string* error = nullptr);
    [[nodiscard]] bool replace_roots(const std::vector<std::filesystem::path>& roots, std::string* error = nullptr);
    [[nodiscard]] std::vector<std::string> roots(std::string* error = nullptr) const;

    void reindex_async();
    void wait_for_idle();

    [[nodiscard]] bool set_pinned(std::string_view utf8_path, bool pinned, std::string* error = nullptr);
    [[nodiscard]] bool set_indexed(std::string_view utf8_path, bool indexed, std::string* error = nullptr);

    [[nodiscard]] Status status() const;
    [[nodiscard]] std::uint64_t generation() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace such::sdk

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <such/ui/ResultView.h>
#include <such/ui/SearchDialect.h>

namespace such::runtime {

struct RuntimeStatus {
    bool indexing = false;
    std::size_t indexed_files = 0;
    std::uint64_t generation = 0;
    std::vector<std::string> roots;
    std::string last_error;
};

struct RuntimeEngineStats {
    std::size_t rows = 0;
    std::size_t chunks = 0;
    std::size_t candidate_chunks = 0;
    std::size_t candidate_rows = 0;
    std::size_t pruned_by_time = 0;
    std::size_t pruned_by_extension = 0;
    std::size_t pruned_by_text = 0;
    std::size_t workers_used = 1;
    bool parallel = false;
    bool backend_loaded = false;
    bool backend_active = false;
    std::size_t backend_candidate_rows = 0;
    std::uint64_t dynamic_queries = 0;
    std::uint64_t dynamic_tiles = 0;
    std::uint64_t direct_compaction_queries = 0;
};

class RuntimeClient final {
public:
    explicit RuntimeClient(std::filesystem::path state_directory = {});
    ~RuntimeClient();

    RuntimeClient(const RuntimeClient&) = delete;
    RuntimeClient& operator=(const RuntimeClient&) = delete;
    RuntimeClient(RuntimeClient&&) = delete;
    RuntimeClient& operator=(RuntimeClient&&) = delete;

    [[nodiscard]] bool load(std::string* error = nullptr);
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] std::string last_error() const;

    // Root mutations are indexing boundaries: the production runtime schedules
    // its asynchronous crawl as part of add/replace. Frontends must not call
    // reindex_async() immediately afterward; use it only for an explicit rebuild.
    [[nodiscard]] bool add_root(const std::filesystem::path& root, std::string* error = nullptr);
    [[nodiscard]] bool remove_root(const std::filesystem::path& root, std::string* error = nullptr);
    [[nodiscard]] bool replace_roots(const std::vector<std::filesystem::path>& roots, std::string* error = nullptr);
    [[nodiscard]] std::vector<std::string> roots(std::string* error = nullptr) const;

    void reindex_async();
    void wait_for_idle();

    [[nodiscard]] std::vector<such::ui::ResultItem> search(
        std::string_view query,
        such::ui::PlatformDialect dialect,
        std::size_t max_results = 0,
        std::string* error = nullptr) const;

    [[nodiscard]] std::vector<std::string> observed_extensions() const;
    [[nodiscard]] bool set_pinned(std::string_view utf8_path, bool pinned, std::string* error = nullptr);
    [[nodiscard]] bool set_indexed(std::string_view utf8_path, bool indexed, std::string* error = nullptr);

    [[nodiscard]] RuntimeStatus status() const;
    [[nodiscard]] RuntimeEngineStats engine_stats() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace such::runtime

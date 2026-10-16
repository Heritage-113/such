#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <such/ui/SearchDialect.h>

namespace such::runtime {

class RuntimeClient;

// Application-service boundary for search-root changes.
//
// RuntimeClient is only the ABI adapter. IndexRootService owns public policy:
// alias resolution, protected-path decisions, fixed exclusions, and deferred
// cleanup of high-churn dependency/cache trees.
class IndexRootService final {
public:
    explicit IndexRootService(
        RuntimeClient& runtime,
        such::ui::PlatformDialect dialect,
        bool deferred_policy_maintenance = true);
    ~IndexRootService();

    IndexRootService(const IndexRootService&) = delete;
    IndexRootService& operator=(const IndexRootService&) = delete;
    IndexRootService(IndexRootService&&) = delete;
    IndexRootService& operator=(IndexRootService&&) = delete;

    [[nodiscard]] bool add_root(const std::filesystem::path& root, std::string* error = nullptr);
    [[nodiscard]] bool replace_roots(const std::vector<std::filesystem::path>& roots, std::string* error = nullptr);
    [[nodiscard]] bool remove_root(const std::filesystem::path& root, std::string* error = nullptr);

    // Re-seed the current root set so stale exclusion state is cleared, apply
    // bounded fixed policy, and let the private runtime rebuild asynchronously.
    // No recursive filesystem walk occurs here.
    [[nodiscard]] bool reindex_async(std::string* error = nullptr);

    // Mark persisted roots for post-index maintenance, e.g. at GUI startup.
    void refresh_policy_async();

    // Called from the frontend's existing runtime-refresh cadence. Maintenance
    // waits for indexing to become idle, then scrubs at most one bounded batch
    // from the runtime index. It never walks the selected drive itself.
    void poll_policy_maintenance();

    [[nodiscard]] bool policy_maintenance_active() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace such::runtime

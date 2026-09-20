#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <such/ui/SearchDialect.h>

namespace such::ui {

enum class IndexCommandKind : std::uint8_t {
    NoCommand,
    AddRoot,
    ReplaceRoot,
    Reindex,
    ShowRoots,
};

struct IndexCommand {
    IndexCommandKind kind = IndexCommandKind::NoCommand;
    bool matched = false;
    std::optional<std::string> argument;
};

[[nodiscard]] IndexCommand parse_index_command(std::string_view query, PlatformDialect dialect);

} // namespace such::ui

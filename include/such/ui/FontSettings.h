#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <such/ui/SearchDialect.h>

namespace such::ui {

struct FontOption {
    std::string alias;
    std::string family;
    std::string label;
};

struct FontCommand {
    bool matched = false;
    bool show_picker = false;
    std::optional<std::string> requested_family;
};

[[nodiscard]] const std::vector<FontOption>& builtin_font_options();
[[nodiscard]] FontCommand parse_font_command(std::string_view query, PlatformDialect dialect);
[[nodiscard]] std::optional<std::string> load_font_preference();
[[nodiscard]] bool save_font_preference(std::string_view family, std::string* error = nullptr);
[[nodiscard]] bool clear_font_preference(std::string* error = nullptr);

} // namespace such::ui

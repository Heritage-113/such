#include <such/ui/IndexCommands.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

namespace such::ui {
namespace {
std::string trim_lower(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) --last;
    std::string out(value.substr(first, last - first));
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::optional<std::string> cleaned_argument(std::string argument) {
    while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.front())) != 0) argument.erase(argument.begin());
    while (!argument.empty() && std::isspace(static_cast<unsigned char>(argument.back())) != 0) argument.pop_back();
    if (argument.size() >= 2) {
        const char first = argument.front();
        const char last = argument.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            argument = argument.substr(1, argument.size() - 2);
        }
    }
    return argument.empty() ? std::nullopt : std::optional<std::string>(std::move(argument));
}
}

IndexCommand parse_index_command(std::string_view query, PlatformDialect dialect) {
    const std::string value = trim_lower(query);
    if (dialect == PlatformDialect::Windows &&
        (value == "%appdata%" || value == "%localappdata%")) {
        return {IndexCommandKind::AddRoot, true, value};
    }
    const std::string prefix(operator_prefix(dialect));
    const std::string index_token = prefix + "index";
    if (value == index_token) return {IndexCommandKind::AddRoot, true, std::nullopt};
    if (value.starts_with(index_token + " ")) {
        std::string original(query);
        std::size_t first = 0;
        while (first < original.size() && std::isspace(static_cast<unsigned char>(original[first])) != 0) ++first;
        const std::size_t argument_start = first + index_token.size() + 1;
        std::string argument = argument_start < original.size() ? original.substr(argument_start) : std::string{};
        return {IndexCommandKind::AddRoot, true, cleaned_argument(std::move(argument))};
    }
    const std::string drive_token = prefix + "drive";
    if (value == drive_token) return {IndexCommandKind::ReplaceRoot, true, std::nullopt};
    if (value.starts_with(drive_token + " ")) {
        std::string original(query);
        std::size_t first = 0;
        while (first < original.size() && std::isspace(static_cast<unsigned char>(original[first])) != 0) ++first;
        const std::size_t argument_start = first + drive_token.size() + 1;
        std::string argument = argument_start < original.size() ? original.substr(argument_start) : std::string{};
        return {IndexCommandKind::ReplaceRoot, true, cleaned_argument(std::move(argument))};
    }
    if (value == prefix + "reindex") return {IndexCommandKind::Reindex, true, std::nullopt};
    if (value == prefix + "roots") return {IndexCommandKind::ShowRoots, true, std::nullopt};
    return {};
}

} // namespace such::ui

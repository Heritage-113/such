#include <such/ui/FontSettings.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <exception>
#include <fstream>

namespace such::ui {
namespace {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) --last;
    return std::string(value.substr(first, last - first));
}

std::string lower_ascii(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
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

std::filesystem::path settings_path() {
#if defined(_WIN32)
    if (const auto local = env_value("LOCALAPPDATA")) {
        return std::filesystem::path(*local) / "Heritage" / "Such" / "settings.ini";
    }
#elif defined(__APPLE__)
    if (const auto home = env_value("HOME")) {
        return std::filesystem::path(*home) / "Library" / "Application Support" / "Heritage" / "Such" / "settings.ini";
    }
#else
    if (const auto xdg = env_value("XDG_CONFIG_HOME")) {
        return std::filesystem::path(*xdg) / "such" / "settings.ini";
    }
    if (const auto home = env_value("HOME")) {
        return std::filesystem::path(*home) / ".config" / "such" / "settings.ini";
    }
#endif
    return std::filesystem::temp_directory_path() / "heritage-such-settings.ini";
}

std::string family_for_alias(std::string_view token) {
    const std::string needle = lower_ascii(trim(token));
    if (needle.empty()) return {};
    if (needle == "system" || needle == "default") return "";
    for (const auto& option : builtin_font_options()) {
        if (needle == lower_ascii(option.alias) || needle == lower_ascii(option.family) || needle == lower_ascii(option.label)) {
            return option.family;
        }
    }
    return trim(token);
}

} // namespace

const std::vector<FontOption>& builtin_font_options() {
    static const std::vector<FontOption> options{
        {"google", "Google Sans Flex", "Google Sans Flex"},
        {"gowun", "Gowun Batang", "Gowun Batang"},
        {"kopub-dotum", "KoPubWorldDotum_Pro", "KoPubWorld Dotum"},
        {"kopub-batang", "KoPubWorldBatang_Pro", "KoPubWorld Batang"},
        {"continuous", "Continuous", "Continuous"},
    };
    return options;
}

FontCommand parse_font_command(std::string_view query, PlatformDialect dialect) {
    FontCommand out;
    const std::string normalized = trim(query);
    const std::string prefix(operator_prefix(dialect));
    const std::string command = prefix + "font";
    if (normalized == command) {
        out.matched = true;
        out.show_picker = true;
        return out;
    }
    if (!normalized.starts_with(command + " ")) return out;

    out.matched = true;
    const std::string requested = trim(std::string_view(normalized).substr(command.size() + 1));
    if (requested.empty()) {
        out.show_picker = true;
        return out;
    }
    out.requested_family = family_for_alias(requested);
    return out;
}

std::optional<std::string> load_font_preference() {
    std::ifstream in(settings_path(), std::ios::binary);
    if (!in) return std::nullopt;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("font=", 0) == 0) return line.substr(5);
    }
    return std::nullopt;
}

bool save_font_preference(std::string_view family, std::string* error) {
    try {
        const auto path = settings_path();
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "could not open settings file";
            return false;
        }
        out << "font=" << family << '\n';
        return static_cast<bool>(out);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

bool clear_font_preference(std::string* error) {
    return save_font_preference("", error);
}

} // namespace such::ui

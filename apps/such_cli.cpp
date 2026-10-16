#include <such/cli/CliApp.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <climits>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string wide_to_utf8(std::wstring_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(INT_MAX)) return {};
    const int source_length = static_cast<int>(value.size());
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), source_length,
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return {};
    std::string out(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), source_length,
            out.data(), required, nullptr, nullptr) != required) {
        return {};
    }
    return out;
}

} // namespace

int wmain(int argc, wchar_t** wide_argv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const wchar_t* value = wide_argv != nullptr ? wide_argv[i] : nullptr;
        storage.push_back(value != nullptr ? wide_to_utf8(value) : std::string{});
    }

    std::vector<char*> utf8_argv;
    utf8_argv.reserve(storage.size());
    for (auto& value : storage) utf8_argv.push_back(value.data());
    return such::cli::run(argc, utf8_argv.data(), such::ui::PlatformDialect::Windows);
}
#else
int main(int argc, char** argv) {
    return such::cli::run(argc, argv, such::ui::PlatformDialect::UnixLike);
}
#endif

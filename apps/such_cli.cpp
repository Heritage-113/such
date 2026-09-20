#include <such/cli/CliApp.h>

int main(int argc, char** argv) {
#if defined(_WIN32)
    constexpr auto dialect = such::ui::PlatformDialect::Windows;
#else
    constexpr auto dialect = such::ui::PlatformDialect::UnixLike;
#endif
    return such::cli::run(argc, argv, dialect);
}

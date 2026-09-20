#pragma once

#include <such/ui/SearchDialect.h>

namespace such::cli {

// GUI frontends can call this before creating windows. No arguments means GUI;
// any non-GUI argument means the invocation is a terminal command/search.
[[nodiscard]] bool should_dispatch_from_gui(int argc, char* const* argv) noexcept;

// Shared terminal entry point used by SuchCLI and the Linux `such` binary.
int run(int argc, char* const* argv, such::ui::PlatformDialect dialect);

} // namespace such::cli

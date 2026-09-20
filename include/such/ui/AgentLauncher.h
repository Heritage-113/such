#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <such/ui/SearchDialect.h>

namespace such::ui {

enum class AgentKind {
    None,
    Claude,
    Codex,
};

struct AgentCommand {
    bool matched = false;
    AgentKind kind = AgentKind::None;
};

[[nodiscard]] AgentCommand parse_agent_command(std::string_view query, PlatformDialect dialect);
[[nodiscard]] std::filesystem::path preferred_agent_working_directory(const std::vector<std::string>& roots);

// Opens a terminal in working_directory, requires the requested official agent
// CLI to already be installed, registers the sibling SuchMCP executable as a local stdio
// MCP server named "such", and starts the agent. The slash command itself is
// explicit user consent for the install/configuration side effects.
[[nodiscard]] bool launch_agent_terminal(
    AgentKind kind,
    const std::filesystem::path& working_directory,
    std::string* error = nullptr);

} // namespace such::ui

#pragma once

#include <cstdint>
#include <string>

namespace such::security {

// Public/frontend-only result for the Security row. This build deliberately
// contains no enterprise security provider integration. Secure Search therefore
// remains fail-closed and routes users to the Enterprise contact flow.
enum class SecureSearchDecision : std::uint8_t {
    Activated = 0,
    Deactivated = 1,
    RequiresEnterpriseActivation = 2,
    Denied = 3,
};

class SecureSearchGate final {
public:
    [[nodiscard]] SecureSearchDecision request_enable(std::string& error);
    [[nodiscard]] SecureSearchDecision request_disable() noexcept;
    [[nodiscard]] bool active() const noexcept { return false; }
};

inline constexpr const char* kEnterpriseSecurityUrl = "https://such.heritage-labs.net";

} // namespace such::security

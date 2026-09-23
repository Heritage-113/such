#include <such/security/SecureSearchGate.h>

namespace such::security {

SecureSearchDecision SecureSearchGate::request_enable(std::string& error) {
    error.clear();
    return SecureSearchDecision::RequiresEnterpriseActivation;
}

SecureSearchDecision SecureSearchGate::request_disable() noexcept {
    return SecureSearchDecision::Deactivated;
}

} // namespace such::security

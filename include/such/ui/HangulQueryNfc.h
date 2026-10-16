#pragma once

#include <string>
#include <string_view>

namespace such::ui {

// Compose canonical Hangul conjoining jamo (Unicode NFC Hangul algorithm).
// Other characters are preserved verbatim; this is not full Unicode NFC.
// Invalid UTF-8 input is also preserved so the runtime owns input rejection.
[[nodiscard]] std::string compose_hangul_query_nfc(std::string_view value);

} // namespace such::ui

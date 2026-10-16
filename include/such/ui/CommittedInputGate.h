#pragma once

#include <string>
#include <utility>

namespace such::ui {

// Frontend-only dispatch gate. IME preedit is never a searchable query.
// The caller owns Unicode normalization (and the native IME owns composition).
// Search engine, query parsing and ABI are deliberately outside this class.
template <typename String>
class CommittedInputGate final {
public:
    void begin_composition() noexcept { composing_ = true; }
    void end_composition() noexcept { composing_ = false; }
    [[nodiscard]] bool composing() const noexcept { return composing_; }

    // Return true exactly once for each changed committed input. An ordinary
    // paste, delete or replacement is handled the same as a key commit.
    [[nodiscard]] bool accept(const String& committed) {
        if (composing_ || (seen_ && committed == last_)) return false;
        last_ = committed;
        seen_ = true;
        return true;
    }

private:
    bool composing_ = false;
    bool seen_ = false;
    String last_;
};

} // namespace such::ui

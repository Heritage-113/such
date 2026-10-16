#pragma once
#include <string_view>

namespace such::runtime {
// Search presentation policy; no filesystem root exclusion is added here.
// The private crawler/index and its durable generations are left unchanged.
[[nodiscard]] bool searchable_filename(std::string_view filename, std::string_view extension) noexcept;
// Content-result admission only; real text detection and pre-crawl exclusion
// must also be enforced by the Private indexer (outside Public ABI v1).
[[nodiscard]] bool searchable_text_filename(std::string_view filename, std::string_view extension) noexcept;
} // namespace such::runtime

#include <such/ui/HangulQueryNfc.h>

#include <cstdint>
#include <vector>

namespace such::ui {
namespace {
constexpr char32_t l_base = 0x1100;
constexpr char32_t v_base = 0x1161;
constexpr char32_t t_base = 0x11A7;
constexpr char32_t s_base = 0xAC00;
constexpr char32_t l_count = 19;
constexpr char32_t v_count = 21;
constexpr char32_t t_count = 28;
constexpr char32_t s_count = l_count * v_count * t_count;

bool decode(std::string_view input, std::vector<char32_t>& out) {
    for (std::size_t i = 0; i < input.size();) {
        const auto lead = static_cast<unsigned char>(input[i]);
        if (lead < 0x80u) { out.push_back(lead); ++i; continue; }
        std::size_t length = 0;
        char32_t value = 0;
        if (lead >= 0xC2u && lead <= 0xDFu) { length = 2; value = lead & 0x1Fu; }
        else if (lead >= 0xE0u && lead <= 0xEFu) { length = 3; value = lead & 0x0Fu; }
        else if (lead >= 0xF0u && lead <= 0xF4u) { length = 4; value = lead & 0x07u; }
        else return false;
        if (length > input.size() - i) return false;
        for (std::size_t j = 1; j < length; ++j) {
            const auto byte = static_cast<unsigned char>(input[i + j]);
            if ((byte & 0xC0u) != 0x80u) return false;
            value = (value << 6u) | (byte & 0x3Fu);
        }
        if ((length == 2 && value < 0x80u) ||
            (length == 3 && value < 0x800u) ||
            (length == 4 && value < 0x10000u) ||
            (value >= 0xD800u && value <= 0xDFFFu) || value > 0x10FFFFu) return false;
        out.push_back(value);
        i += length;
    }
    return true;
}

void append_utf8(std::string& out, char32_t codepoint) {
    if (codepoint < 0x80u) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800u) {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint < 0x10000u) {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}
} // namespace

std::string compose_hangul_query_nfc(std::string_view input) {
    // Precomposed Hangul and non-Korean queries incur no decoding allocation.
    if (input.find("\xE1\x84") == std::string_view::npos &&
        input.find("\xE1\x85") == std::string_view::npos &&
        input.find("\xE1\x86") == std::string_view::npos) return std::string(input);

    std::vector<char32_t> decoded;
    decoded.reserve(input.size());
    if (!decode(input, decoded)) return std::string(input);

    std::vector<char32_t> composed;
    composed.reserve(decoded.size());
    for (const auto c : decoded) {
        if (!composed.empty()) {
            auto& previous = composed.back();
            if (previous >= l_base && previous < l_base + l_count &&
                c >= v_base && c < v_base + v_count) {
                previous = s_base + ((previous - l_base) * v_count + c - v_base) * t_count;
                continue;
            }
            if (previous >= s_base && previous < s_base + s_count &&
                (previous - s_base) % t_count == 0 && c > t_base && c < t_base + t_count) {
                previous += c - t_base;
                continue;
            }
        }
        composed.push_back(c);
    }
    std::string result;
    result.reserve(input.size());
    for (const auto c : composed) append_utf8(result, c);
    return result;
}

} // namespace such::ui

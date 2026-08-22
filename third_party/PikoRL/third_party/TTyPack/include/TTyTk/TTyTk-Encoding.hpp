#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ttytk::utf8 {

inline constexpr char32_t replacement_character = U'\uFFFD';

namespace detail {

[[nodiscard]] constexpr auto continuation(const unsigned char byte) noexcept -> bool {
    return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] inline auto decode_one(const std::string_view input,
                                     const std::size_t offset,
                                     std::size_t& consumed) noexcept -> char32_t {
    const auto first = static_cast<unsigned char>(input[offset]);
    consumed = 1;
    if (first <= 0x7FU) {
        return static_cast<char32_t>(first);
    }

    std::size_t length = 0;
    char32_t codepoint = 0;
    char32_t minimum = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
        length = 2;
        codepoint = static_cast<char32_t>(first & 0x1FU);
        minimum = 0x80;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        length = 3;
        codepoint = static_cast<char32_t>(first & 0x0FU);
        minimum = 0x800;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        length = 4;
        codepoint = static_cast<char32_t>(first & 0x07U);
        minimum = 0x10000;
    } else {
        return replacement_character;
    }

    if (input.size() - offset < length) {
        return replacement_character;
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto byte = static_cast<unsigned char>(input[offset + index]);
        if (!continuation(byte)) {
            return replacement_character;
        }
        codepoint = static_cast<char32_t>((codepoint << 6U) | (byte & 0x3FU));
    }

    if (codepoint < minimum || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return replacement_character;
    }
    consumed = length;
    return codepoint;
}

}  // namespace detail

// Unicode Standard, UTF-8 definition D92: malformed subsequences produce U+FFFD.
[[nodiscard]] inline auto decode(const std::string_view input) -> std::vector<char32_t> {
    std::vector<char32_t> output;
    output.reserve(input.size());
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t consumed = 0;
        output.push_back(detail::decode_one(input, offset, consumed));
        offset += consumed;
    }
    return output;
}

[[nodiscard]] inline auto decode(const Span<const std::byte> input) -> std::vector<char32_t> {
    std::string bytes;
    bytes.reserve(input.size());
    for (const auto byte : input) {
        bytes.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return decode(bytes);
}

[[nodiscard]] inline auto encode(const char32_t codepoint) -> std::string {
    const auto scalar = (codepoint > 0x10FFFF ||
                         (codepoint >= 0xD800 && codepoint <= 0xDFFF))
                            ? replacement_character
                            : codepoint;
    std::string output;
    if (scalar <= 0x7F) {
        output.push_back(static_cast<char>(scalar));
    } else if (scalar <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0U | (scalar >> 6U)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else if (scalar <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0U | (scalar >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else {
        output.push_back(static_cast<char>(0xF0U | (scalar >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        output.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    }
    return output;
}

[[nodiscard]] inline auto encode(const Span<const char32_t> codepoints) -> std::string {
    std::string output;
    for (const auto codepoint : codepoints) {
        output += encode(codepoint);
    }
    return output;
}

[[nodiscard]] inline auto validate(const std::string_view input) noexcept -> bool {
    for (std::size_t offset = 0; offset < input.size();) {
        std::size_t consumed = 0;
        if (detail::decode_one(input, offset, consumed) == replacement_character &&
            static_cast<unsigned char>(input[offset]) != 0xEFU) {
            return false;
        }
        if (consumed == 1 && static_cast<unsigned char>(input[offset]) >= 0x80U) {
            return false;
        }
        offset += consumed;
    }
    return true;
}

}  // namespace ttytk::utf8

namespace ttytk::cp {

// NOTE(agent): The manifest requires selecting Latin-1 or CP437 but exposes no
// selector type. Codepage is the minimal public type needed by both conversions.
enum class Codepage { latin1, cp437 };

namespace detail {

inline constexpr std::array<char32_t, 128> cp437_upper = {
    U'\u00C7', U'\u00FC', U'\u00E9', U'\u00E2', U'\u00E4', U'\u00E0', U'\u00E5', U'\u00E7',
    U'\u00EA', U'\u00EB', U'\u00E8', U'\u00EF', U'\u00EE', U'\u00EC', U'\u00C4', U'\u00C5',
    U'\u00C9', U'\u00E6', U'\u00C6', U'\u00F4', U'\u00F6', U'\u00F2', U'\u00FB', U'\u00F9',
    U'\u00FF', U'\u00D6', U'\u00DC', U'\u00A2', U'\u00A3', U'\u00A5', U'\u20A7', U'\u0192',
    U'\u00E1', U'\u00ED', U'\u00F3', U'\u00FA', U'\u00F1', U'\u00D1', U'\u00AA', U'\u00BA',
    U'\u00BF', U'\u2310', U'\u00AC', U'\u00BD', U'\u00BC', U'\u00A1', U'\u00AB', U'\u00BB',
    U'\u2591', U'\u2592', U'\u2593', U'\u2502', U'\u2524', U'\u2561', U'\u2562', U'\u2556',
    U'\u2555', U'\u2563', U'\u2551', U'\u2557', U'\u255D', U'\u255C', U'\u255B', U'\u2510',
    U'\u2514', U'\u2534', U'\u252C', U'\u251C', U'\u2500', U'\u253C', U'\u255E', U'\u255F',
    U'\u255A', U'\u2554', U'\u2569', U'\u2566', U'\u2560', U'\u2550', U'\u256C', U'\u2567',
    U'\u2568', U'\u2564', U'\u2565', U'\u2559', U'\u2558', U'\u2552', U'\u2553', U'\u256B',
    U'\u256A', U'\u2518', U'\u250C', U'\u2588', U'\u2584', U'\u258C', U'\u2590', U'\u2580',
    U'\u03B1', U'\u00DF', U'\u0393', U'\u03C0', U'\u03A3', U'\u03C3', U'\u00B5', U'\u03C4',
    U'\u03A6', U'\u0398', U'\u03A9', U'\u03B4', U'\u221E', U'\u03C6', U'\u03B5', U'\u2229',
    U'\u2261', U'\u00B1', U'\u2265', U'\u2264', U'\u2320', U'\u2321', U'\u00F7', U'\u2248',
    U'\u00B0', U'\u2219', U'\u00B7', U'\u221A', U'\u207F', U'\u00B2', U'\u25A0', U'\u00A0'};

[[nodiscard]] constexpr auto decode_byte(const unsigned char byte,
                                         const Codepage page) noexcept -> char32_t {
    if (byte < 0x80U) {
        return static_cast<char32_t>(byte);
    }
    return page == Codepage::latin1 ? static_cast<char32_t>(byte)
                                    : cp437_upper[byte - 0x80U];
}

}  // namespace detail

[[nodiscard]] inline auto to_utf8(const std::string_view input,
                                  const Codepage page = Codepage::latin1) -> std::string {
    std::string output;
    for (const unsigned char byte : input) {
        output += utf8::encode(detail::decode_byte(byte, page));
    }
    return output;
}

[[nodiscard]] inline auto from_utf8(const std::string_view input,
                                    const Codepage page = Codepage::latin1) -> std::string {
    std::string output;
    for (const auto codepoint : utf8::decode(input)) {
        if (codepoint <= 0x7F) {
            output.push_back(static_cast<char>(codepoint));
            continue;
        }
        if (page == Codepage::latin1 && codepoint <= 0xFF) {
            output.push_back(static_cast<char>(codepoint));
            continue;
        }
        bool found = false;
        if (page == Codepage::cp437) {
            for (std::size_t index = 0; index < detail::cp437_upper.size(); ++index) {
                if (detail::cp437_upper[index] == codepoint) {
                    output.push_back(static_cast<char>(index + 0x80U));
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            output.push_back('?');
        }
    }
    return output;
}

}  // namespace ttytk::cp

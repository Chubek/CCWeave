#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Automaton.hpp>

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ttytk::input {

struct Key {
    char32_t codepoint{};
    unsigned modifiers{};
    unsigned event_type{1};
};

struct Mouse {
    unsigned button{};
    unsigned modifiers{};
    unsigned x{};
    unsigned y{};
    bool pressed{};
    bool motion{};
};

struct Paste {
    std::string text{};
};

using Event = std::variant<Key, Mouse, Paste>;

namespace detail {

[[nodiscard]] inline auto number(const std::string_view text, unsigned& value) -> bool {
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), value);
    return converted.ec == std::errc{} && converted.ptr == text.data() + text.size();
}

[[nodiscard]] inline auto numbers(const std::string_view text) -> std::vector<unsigned> {
    std::vector<unsigned> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find(';', start);
        const auto part = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        unsigned value = 0;
        if (!number(part, value)) return {};
        result.push_back(value);
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

}  // namespace detail

// kitty keyboard protocol, "The central escape code": CSI code;mods:event u.
// xterm ctlseqs, SGR (1006): CSI <Cb;Cx;Cy M/m.
[[nodiscard]] inline auto decode_seq(const std::string_view sequence) -> Result<Event> {
    constexpr std::string_view csi{"\x1B["};
    constexpr std::string_view paste_start{"\x1B[200~"};
    constexpr std::string_view paste_end{"\x1B[201~"};
    if (sequence.starts_with(paste_start) && sequence.ends_with(paste_end)) {
        return Event{Paste{std::string{sequence.substr(
            paste_start.size(), sequence.size() - paste_start.size() - paste_end.size())}}};
    }
    if (!sequence.starts_with(csi) || sequence.size() < csi.size() + 2) {
        return Error{1, "unsupported input sequence"};
    }
    const auto body = sequence.substr(csi.size());
    const auto final = body.back();
    const auto parameters = body.substr(0, body.size() - 1);
    if (final == 'u') {
        const auto separator = parameters.find(';');
        const auto code_field = parameters.substr(0, separator);
        const auto colon_in_code = code_field.find(':');
        unsigned codepoint = 0;
        if (!detail::number(code_field.substr(0, colon_in_code), codepoint)) {
            return Error{2, "invalid CSI u sequence"};
        }
        unsigned modifier_field = 1;
        unsigned event_type = 1;
        if (separator != std::string_view::npos) {
            const auto modifier_and_event = parameters.substr(separator + 1);
            const auto colon = modifier_and_event.find(':');
            if (!detail::number(modifier_and_event.substr(0, colon), modifier_field)) {
                return Error{2, "invalid CSI u sequence"};
            }
            if (colon != std::string_view::npos &&
                !detail::number(modifier_and_event.substr(colon + 1), event_type)) {
                return Error{2, "invalid CSI u sequence"};
            }
        }
        return Event{Key{static_cast<char32_t>(codepoint),
                         modifier_field == 0 ? 0 : modifier_field - 1, event_type}};
    }
    if ((final == 'M' || final == 'm') && parameters.starts_with('<')) {
        const auto values = detail::numbers(parameters.substr(1));
        if (values.size() != 3) return Error{3, "invalid SGR mouse sequence"};
        const auto encoded = values[0];
        return Event{Mouse{encoded & 3U, encoded & (4U | 8U | 16U), values[1], values[2],
                           final == 'M', (encoded & 32U) != 0}};
    }
    return Error{1, "unsupported input sequence"};
}

}  // namespace ttytk::input

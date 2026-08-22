#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Encoding.hpp>

#include <cstddef>
#include <iterator>
#include <string_view>
#include <vector>

namespace ttytk::grapheme {

namespace detail {

enum class Property { other, cr, lf, control, extend, zwj, regional_indicator, prepend, spacing_mark,
                      l, v, t, lv, lvt, extended_pictographic };

[[nodiscard]] constexpr auto between(const char32_t value, const char32_t first,
                                     const char32_t last) noexcept -> bool {
    return value >= first && value <= last;
}

[[nodiscard]] constexpr auto property(const char32_t value) noexcept -> Property {
    if (value == U'\r') return Property::cr;
    if (value == U'\n') return Property::lf;
    if (value == 0x200DU) return Property::zwj;
    if (between(value, 0x1F1E6, 0x1F1FF)) return Property::regional_indicator;
    if (between(value, 0x1100, 0x115F) || between(value, 0xA960, 0xA97C)) return Property::l;
    if (between(value, 0x1160, 0x11A7) || between(value, 0xD7B0, 0xD7C6)) return Property::v;
    if (between(value, 0x11A8, 0x11FF) || between(value, 0xD7CB, 0xD7FB)) return Property::t;
    if (between(value, 0xAC00, 0xD7A3)) {
        return (value - 0xAC00) % 28 == 0 ? Property::lv : Property::lvt;
    }
    if (between(value, 0x0600, 0x0605) || value == 0x06DD || value == 0x070F ||
        value == 0x0890 || value == 0x0891 || value == 0x08E2 ||
        between(value, 0x0D4E, 0x0D4E) || value == 0x110BD ||
        between(value, 0x111C2, 0x111C3) || between(value, 0x1193F, 0x1193F) ||
        between(value, 0x11941, 0x11941) || between(value, 0x11A3A, 0x11A3A) ||
        between(value, 0x11A84, 0x11A89) || value == 0x11D46) return Property::prepend;
    if (between(value, 0x0903, 0x0903) || between(value, 0x093B, 0x093B) ||
        between(value, 0x093E, 0x0940) || between(value, 0x0949, 0x094C) ||
        between(value, 0x0982, 0x0983) || between(value, 0x09BE, 0x09C0) ||
        between(value, 0x0A3E, 0x0A40) || between(value, 0x0AC9, 0x0AC9) ||
        between(value, 0x0B3E, 0x0B3E) || between(value, 0x0BBE, 0x0BBF) ||
        between(value, 0x0C01, 0x0C03) || between(value, 0x0CC0, 0x0CC4) ||
        between(value, 0x0D3E, 0x0D40) || between(value, 0x0DCF, 0x0DD1) ||
        between(value, 0x0F3E, 0x0F3F) || between(value, 0x102B, 0x102C) ||
        between(value, 0x17B6, 0x17B6) || between(value, 0x1923, 0x1926) ||
        between(value, 0x1A19, 0x1A1A) || between(value, 0x1B3D, 0x1B41) ||
        between(value, 0x1B43, 0x1B44) || between(value, 0xA823, 0xA824) ||
        between(value, 0x11000, 0x11000) || between(value, 0x1112C, 0x1112C))
        return Property::spacing_mark;
    if (value == 0x200CU || between(value, 0x0300, 0x036F) ||
        between(value, 0x0483, 0x0489) || between(value, 0x0591, 0x05BD) ||
        between(value, 0x05BF, 0x05BF) || between(value, 0x05C1, 0x05C2) ||
        between(value, 0x0610, 0x061A) || between(value, 0x064B, 0x065F) ||
        between(value, 0x06D6, 0x06ED) || between(value, 0x0730, 0x074A) ||
        between(value, 0x07A6, 0x07B0) || between(value, 0x07EB, 0x07F3) ||
        between(value, 0x0816, 0x082D) || between(value, 0x0859, 0x085B) ||
        between(value, 0x08D3, 0x0902) || between(value, 0x093A, 0x093C) ||
        between(value, 0x0941, 0x0948) || between(value, 0x094D, 0x094D) ||
        between(value, 0x0951, 0x0957) || between(value, 0x0962, 0x0963) ||
        between(value, 0x1AB0, 0x1ACE) || between(value, 0x1DC0, 0x1DFF) ||
        between(value, 0x20D0, 0x20F0) || between(value, 0xFE00, 0xFE0F) ||
        between(value, 0xFE20, 0xFE2F) || between(value, 0xE0100, 0xE01EF))
        return Property::extend;
    if (between(value, 0x1F000, 0x1FAFF) || between(value, 0x2600, 0x27BF) ||
        between(value, 0x2300, 0x23FF)) return Property::extended_pictographic;
    if (value <= 0x001FU || between(value, 0x007F, 0x009F) ||
        between(value, 0xD800, 0xDFFF)) return Property::control;
    return Property::other;
}

struct Codepoint {
    char32_t value;
    std::size_t end;
};

[[nodiscard]] inline auto next_codepoint(const std::string_view text,
                                         const std::size_t start) -> Codepoint {
    const auto first = static_cast<unsigned char>(text[start]);
    std::size_t length = 1;
    if (first >= 0xC2U && first <= 0xDFU) length = 2;
    else if (first >= 0xE0U && first <= 0xEFU) length = 3;
    else if (first >= 0xF0U && first <= 0xF4U) length = 4;
    if (start + length <= text.size() && utf8::validate(text.substr(start, length))) {
        return {utf8::decode(text.substr(start, length)).front(), start + length};
    }
    return {utf8::replacement_character, start + 1};
}

[[nodiscard]] constexpr auto no_break(const Property previous, const Property current,
                                      const bool previous_zwj_after_pictographic,
                                      const std::size_t regional_before) noexcept -> bool {
    // UAX #29, rules GB3–GB13 (Unicode 15.1).
    if (previous == Property::cr && current == Property::lf) return true;              // GB3
    if (previous == Property::cr || previous == Property::lf || previous == Property::control ||
        current == Property::cr || current == Property::lf || current == Property::control)
        return false;                                                                    // GB4/GB5
    if (previous == Property::l &&
        (current == Property::l || current == Property::v || current == Property::lv ||
         current == Property::lvt)) return true;                                         // GB6
    if ((previous == Property::lv || previous == Property::v) &&
        (current == Property::v || current == Property::t)) return true;                // GB7
    if ((previous == Property::lvt || previous == Property::t) && current == Property::t)
        return true;                                                                      // GB8
    if (current == Property::extend || current == Property::zwj) return true;           // GB9
    if (current == Property::spacing_mark) return true;                                  // GB9a
    if (previous == Property::prepend) return true;                                      // GB9b
    if (previous_zwj_after_pictographic && current == Property::extended_pictographic)
        return true;                                                                      // GB11
    return previous == Property::regional_indicator &&
           current == Property::regional_indicator && (regional_before % 2U) == 1U;     // GB12/13
}

}  // namespace detail

[[nodiscard]] inline auto next_break(const std::string_view text,
                                     const std::size_t start = 0) -> std::size_t {
    if (start >= text.size()) return text.size();
    auto previous = detail::next_codepoint(text, start);
    auto previous_property = detail::property(previous.value);
    std::size_t offset = previous.end;
    std::size_t regional_count = previous_property == detail::Property::regional_indicator ? 1U : 0U;
    bool pictographic_before_zwj = false;
    bool previous_zwj_after_pictographic = false;
    bool pictographic_sequence = previous_property == detail::Property::extended_pictographic;

    while (offset < text.size()) {
        const auto current = detail::next_codepoint(text, offset);
        const auto current_property = detail::property(current.value);
        if (!detail::no_break(previous_property, current_property, previous_zwj_after_pictographic,
                              regional_count)) {
            return offset;
        }
        if (current_property == detail::Property::regional_indicator) ++regional_count;
        else if (current_property != detail::Property::extend) regional_count = 0;

        pictographic_before_zwj = pictographic_sequence;
        previous_zwj_after_pictographic =
            current_property == detail::Property::zwj && pictographic_before_zwj;
        if (current_property == detail::Property::extended_pictographic) pictographic_sequence = true;
        else if (current_property != detail::Property::extend && current_property != detail::Property::zwj)
            pictographic_sequence = false;

        previous_property = current_property;
        offset = current.end;
    }
    return text.size();
}

[[nodiscard]] inline auto segment(const std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> result;
    for (std::size_t start = 0; start < text.size();) {
        const auto end = next_break(text, start);
        result.emplace_back(text.substr(start, end - start));
        start = end;
    }
    return result;
}

[[nodiscard]] inline auto count(const std::string_view text) -> std::size_t {
    std::size_t result = 0;
    for (std::size_t start = 0; start < text.size(); start = next_break(text, start)) ++result;
    return result;
}

class Iterator {
public:
    using value_type = std::string_view;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;

    constexpr Iterator() = default;
    constexpr Iterator(const std::string_view text, const std::size_t offset) noexcept
        : text_(text), offset_(offset) {}

    [[nodiscard]] auto operator*() const -> value_type {
        const auto end = next_break(text_, offset_);
        return text_.substr(offset_, end - offset_);
    }

    auto operator++() -> Iterator& {
        offset_ = next_break(text_, offset_);
        return *this;
    }

    auto operator++(int) -> Iterator {
        const auto copy = *this;
        ++*this;
        return copy;
    }

    [[nodiscard]] friend constexpr auto operator==(const Iterator& lhs,
                                                   const Iterator& rhs) noexcept -> bool {
        return lhs.text_.data() == rhs.text_.data() && lhs.text_.size() == rhs.text_.size() &&
               lhs.offset_ == rhs.offset_;
    }

private:
    std::string_view text_{};
    std::size_t offset_{};
};

}  // namespace ttytk::grapheme

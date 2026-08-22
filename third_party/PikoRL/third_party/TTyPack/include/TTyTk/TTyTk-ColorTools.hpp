#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ttytk::color {

struct Rgb {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};

    [[nodiscard]] friend constexpr auto operator==(const Rgb&, const Rgb&) -> bool = default;
};

namespace detail {

inline constexpr std::array<Rgb, 16> ansi_16 = {{
    {0, 0, 0},       {205, 49, 49},   {13, 188, 121}, {229, 229, 16},
    {36, 114, 200},  {188, 63, 188},  {17, 168, 205}, {229, 229, 229},
    {102, 102, 102}, {241, 76, 76},   {35, 209, 139}, {245, 245, 67},
    {59, 142, 234},  {214, 112, 214}, {41, 184, 219}, {255, 255, 255},
}};

[[nodiscard]] constexpr auto cube_value(const unsigned int index) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(index == 0 ? 0 : 55 + index * 40);
}

[[nodiscard]] constexpr auto palette_256(const std::uint8_t index) noexcept -> Rgb {
    if (index < 16) return ansi_16[index];
    if (index >= 232) {
        const auto shade = static_cast<std::uint8_t>(8 + (index - 232) * 10);
        return {shade, shade, shade};
    }
    const auto value = static_cast<unsigned int>(index - 16);
    return {cube_value(value / 36), cube_value((value / 6) % 6), cube_value(value % 6)};
}

[[nodiscard]] constexpr auto hex_value(const char value) noexcept -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] constexpr auto equal_ignore_case(const std::string_view lhs,
                                               const std::string_view rhs) noexcept -> bool {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const auto left = lhs[index] >= 'A' && lhs[index] <= 'Z'
                              ? static_cast<char>(lhs[index] - 'A' + 'a')
                              : lhs[index];
        if (left != rhs[index]) return false;
    }
    return true;
}

}  // namespace detail

[[nodiscard]] inline auto distance(const Rgb lhs, const Rgb rhs) noexcept -> double {
    const auto red = static_cast<double>(lhs.red) - static_cast<double>(rhs.red);
    const auto green = static_cast<double>(lhs.green) - static_cast<double>(rhs.green);
    const auto blue = static_cast<double>(lhs.blue) - static_cast<double>(rhs.blue);
    // Weighted Euclidean distance approximates relative luminance sensitivity.
    return std::sqrt(0.299 * red * red + 0.587 * green * green + 0.114 * blue * blue);
}

[[nodiscard]] inline auto to_256(const Rgb rgb) noexcept -> std::uint8_t {
    std::uint8_t nearest = 0;
    auto nearest_distance = distance(rgb, detail::palette_256(0));
    for (unsigned int index = 1; index < 256; ++index) {
        const auto candidate = distance(rgb, detail::palette_256(static_cast<std::uint8_t>(index)));
        if (candidate < nearest_distance) {
            nearest = static_cast<std::uint8_t>(index);
            nearest_distance = candidate;
        }
    }
    return nearest;
}

[[nodiscard]] inline auto to_16(const Rgb rgb) noexcept -> std::uint8_t {
    std::uint8_t nearest = 0;
    auto nearest_distance = distance(rgb, detail::ansi_16[0]);
    for (std::size_t index = 1; index < detail::ansi_16.size(); ++index) {
        const auto candidate = distance(rgb, detail::ansi_16[index]);
        if (candidate < nearest_distance) {
            nearest = static_cast<std::uint8_t>(index);
            nearest_distance = candidate;
        }
    }
    return nearest;
}

[[nodiscard]] inline auto luminance(const Rgb rgb) noexcept -> double {
    return (0.2126 * static_cast<double>(rgb.red) + 0.7152 * static_cast<double>(rgb.green) +
            0.0722 * static_cast<double>(rgb.blue)) /
           255.0;
}

[[nodiscard]] inline auto parse(const std::string_view value) noexcept -> std::optional<Rgb> {
    if (value.size() == 7 && value[0] == '#') {
        const auto r1 = detail::hex_value(value[1]);
        const auto r2 = detail::hex_value(value[2]);
        const auto g1 = detail::hex_value(value[3]);
        const auto g2 = detail::hex_value(value[4]);
        const auto b1 = detail::hex_value(value[5]);
        const auto b2 = detail::hex_value(value[6]);
        if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) return std::nullopt;
        return Rgb{static_cast<std::uint8_t>(r1 * 16 + r2),
                   static_cast<std::uint8_t>(g1 * 16 + g2),
                   static_cast<std::uint8_t>(b1 * 16 + b2)};
    }

    struct Named {
        std::string_view name;
        Rgb rgb;
    };
    constexpr std::array names = {
        Named{"black", {0, 0, 0}},       Named{"white", {255, 255, 255}},
        Named{"red", {255, 0, 0}},       Named{"green", {0, 128, 0}},
        Named{"blue", {0, 0, 255}},      Named{"yellow", {255, 255, 0}},
        Named{"magenta", {255, 0, 255}}, Named{"cyan", {0, 255, 255}},
        Named{"gray", {128, 128, 128}},  Named{"grey", {128, 128, 128}},
    };
    for (const auto named : names) {
        if (detail::equal_ignore_case(value, named.name)) return named.rgb;
    }
    return std::nullopt;
}

}  // namespace ttytk::color

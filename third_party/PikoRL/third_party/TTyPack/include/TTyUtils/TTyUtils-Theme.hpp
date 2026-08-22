#pragma once

#include <TTyTk/TTyTk-ColorTools.hpp>
#include <TTyUtils/TTyUtils-Config.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ttyutils::theme {

struct Style {
    std::optional<ttytk::color::Rgb> foreground{};
    std::optional<ttytk::color::Rgb> background{};
    std::uint32_t attributes{};

    [[nodiscard]] friend auto operator==(const Style&, const Style&) -> bool = default;
};

class Palette {
public:
    void set(std::string name, ttytk::color::Rgb value) { colors_[std::move(name)] = value; }
    [[nodiscard]] auto get(const std::string_view name) const -> std::optional<ttytk::color::Rgb> {
        const auto found = colors_.find(std::string(name));
        return found == colors_.end() ? std::nullopt : std::optional{found->second};
    }
    [[nodiscard]] auto quantize(const ttytk::color::Rgb color, const unsigned colors = 256) const noexcept
        -> ttytk::color::Rgb {
        if (colors <= 16) return ttytk::color::detail::ansi_16[ttytk::color::to_16(color)];
        if (colors <= 256) {
            const auto index = ttytk::color::to_256(color);
            const auto parsed = ttytk::color::parse("#000000");
            (void)parsed;
            const auto red = static_cast<std::uint8_t>(index < 16 ? index * 16 : index);
            return {red, red, red};
        }
        return color;
    }

private:
    std::unordered_map<std::string, ttytk::color::Rgb> colors_;
};

class Theme {
public:
    [[nodiscard]] auto role(const std::string_view name) const -> Style {
        const auto found = roles_.find(std::string(name));
        return found == roles_.end() ? Style{} : found->second;
    }
    void set_role(std::string name, Style style) { roles_[std::move(name)] = style; }
    [[nodiscard]] auto palette() const noexcept -> const Palette& { return palette_; }
    [[nodiscard]] auto palette() noexcept -> Palette& { return palette_; }
    void set_dark_mode(const bool value) noexcept { dark_ = value; }
    [[nodiscard]] auto dark_mode() const noexcept -> bool { return dark_; }

private:
    std::unordered_map<std::string, Style> roles_;
    Palette palette_;
    bool dark_{};
};

namespace detail {

[[nodiscard]] inline auto trim(std::string_view value) -> std::string_view {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) value.remove_suffix(1);
    return value;
}

}  // namespace detail

[[nodiscard]] inline auto load(const std::filesystem::path& path) -> Theme {
    Theme result;
    std::ifstream input(path);
    if (!input) return result;
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = detail::trim(std::string_view(line).substr(0, equals));
        auto value = detail::trim(std::string_view(line).substr(equals + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (key == "dark") {
            result.set_dark_mode(value == "true" || value == "1");
            continue;
        }
        const auto dot = key.rfind('.');
        if (dot == std::string_view::npos) continue;
        const auto role_name = key.substr(0, dot);
        auto style = result.role(role_name);
        const auto field = key.substr(dot + 1);
        if (const auto color = ttytk::color::parse(value)) {
            if (field == "fg" || field == "foreground") style.foreground = *color;
            if (field == "bg" || field == "background") style.background = *color;
            result.set_role(std::string(role_name), style);
        }
    }
    return result;
}

// NOTE(agent): The manifest exposes role/dark_mode as namespace symbols while
// the example uses Theme members. Both forms are supplied for compatibility.
[[nodiscard]] inline auto role(const Theme& theme, const std::string_view name) -> Style {
    return theme.role(name);
}
[[nodiscard]] inline auto dark_mode(const Theme& theme) noexcept -> bool {
    return theme.dark_mode();
}

}  // namespace ttyutils::theme

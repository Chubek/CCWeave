#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-ColorTools.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>
#include <array>
#include <optional>
#include <string>
#include <string_view>
namespace ttytk::theme {
struct Theme { std::string name{}; std::array<color::Rgb,16> palette{}; color::Rgb foreground{229,229,229}; color::Rgb background{}; };
[[nodiscard]] inline auto builtin(std::string_view name)->Theme { Theme theme; theme.name=std::string{name}; if(name=="solarized-dark"){theme.background={0,43,54};theme.foreground={131,148,150};} return theme; }
[[nodiscard]] inline auto load(std::string_view name)->std::optional<Theme> { return name.empty()?std::nullopt:std::optional<Theme>{builtin(name)}; }
[[nodiscard]] inline auto apply(const Theme& theme)->std::string { std::string out; for(std::size_t i=0;i<theme.palette.size();++i){auto c=theme.palette[i];out+=std::string{esc::osc}+"4;"+std::to_string(i)+";rgb:"+std::to_string(c.red)+"/"+std::to_string(c.green)+"/"+std::to_string(c.blue)+"\x1B\\";} return out; }
} // namespace ttytk::theme

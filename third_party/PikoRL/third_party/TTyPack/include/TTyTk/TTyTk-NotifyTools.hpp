#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-OSCTools.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace ttytk::notify {

// wezterm escape sequences: OSC 777;notify;title;body ST.
[[nodiscard]] inline auto show(const std::string_view title, const std::string_view body = {})
    -> std::string {
    return "\x1b]777;notify;" + std::string{title} + ';' + std::string{body} + "\x1b\\";
}

// NOTE(agent): The manifest does not choose among incompatible progress
// protocols. This follows the documented iTerm2/ConEmu-compatible OSC 9 form.
[[nodiscard]] inline auto progress(const unsigned value, const unsigned total) -> std::string {
    const auto percent = total == 0 ? 0U : std::min(100U, (value * 100U) / total);
    return "\x1b]9;4;1;" + std::to_string(percent) + "\x1b\\";
}

[[nodiscard]] inline auto badge(const std::string_view text) -> std::string {
    return "\x1b]1337;SetBadgeFormat=" + std::string{text} + "\x07";
}

}  // namespace ttytk::notify

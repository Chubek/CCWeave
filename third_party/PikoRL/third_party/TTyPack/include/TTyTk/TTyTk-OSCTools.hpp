#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <string>
#include <string_view>

namespace ttytk::osc {

namespace detail {

[[nodiscard]] inline auto command(const std::string_view body) -> std::string {
    return std::string{esc::osc} + std::string{body} + "\x1B\\";
}

}  // namespace detail

// xterm ctlseqs: OSC 2 sets the window title.
[[nodiscard]] inline auto set_title(const std::string_view title) -> std::string {
    return detail::command("2;" + std::string{title});
}

// xterm ctlseqs: OSC 8 ; params ; URI ST starts or ends hyperlinks.
[[nodiscard]] inline auto hyperlink(const std::string_view uri, const std::string_view text) -> std::string {
    return detail::command("8;;" + std::string{uri}) + std::string{text} + detail::command("8;;");
}

// xterm ctlseqs: OSC 9 ; Pt is a desktop-notification extension.
[[nodiscard]] inline auto desktop_notify(const std::string_view message) -> std::string {
    return detail::command("9;" + std::string{message});
}

// xterm ctlseqs: OSC 4 ; c ; ? queries palette entry c.
[[nodiscard]] inline auto palette_query(const unsigned index) -> std::string {
    return detail::command("4;" + std::to_string(index) + ";?");
}

}  // namespace ttytk::osc

#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <initializer_list>
#include <string>
#include <string_view>

namespace ttytk::esc {

// ECMA-48 §5.4: 7-bit representations of the format effectors.
inline constexpr std::string_view csi{"\x1B["};
inline constexpr std::string_view osc{"\x1B]"};
inline constexpr std::string_view dcs{"\x1BP"};

// NOTE(agent): The manifest's sgr example requires attribute values but does
// not list them. These scoped constants are the minimal SGR parameter surface.
enum Sgr : unsigned {
    Reset = 0,
    Bold = 1,
    Faint = 2,
    Italic = 3,
    Underline = 4,
    Blink = 5,
    Inverse = 7,
    Conceal = 8,
    Strike = 9,
};

[[nodiscard]] inline auto sgr(const std::initializer_list<unsigned> parameters) -> std::string {
    std::string result{csi};
    bool first = true;
    for (const auto parameter : parameters) {
        if (!first) result.push_back(';');
        result += std::to_string(parameter);
        first = false;
    }
    result.push_back('m');
    return result;
}

[[nodiscard]] inline auto sgr(const Sgr parameter) -> std::string {
    return sgr({static_cast<unsigned>(parameter)});
}

// ECMA-48 §8.3.5, CUP: coordinates are one-based and default to 1.
[[nodiscard]] inline auto cursor_to(const unsigned row, const unsigned column) -> std::string {
    return std::string{csi} + std::to_string(row == 0 ? 1 : row) + ';' +
           std::to_string(column == 0 ? 1 : column) + 'H';
}

// xterm ctlseqs: ED 2 erases the complete display.
[[nodiscard]] inline auto clear() -> std::string {
    return std::string{csi} + "2J";
}

// xterm ctlseqs: OSC 8 hyperlinks use OSC 8 ; params ; URI ST, with an empty
// URI closing the active hyperlink.
struct Hyperlink {
    [[nodiscard]] static inline auto open(const std::string_view uri) -> std::string {
        return std::string{osc} + "8;;" + std::string(uri) + "\x1b\\";
    }
    [[nodiscard]] static inline auto close() -> std::string { return std::string{osc} + "8;;\x1b\\"; }
};

}  // namespace ttytk::esc

// NOTE(agent): TTyUtils manifests name TTyTk::Hyperlink; keep a global alias
// to the escape-code-owned builder without adding a new header.
namespace ttytk {
using Hyperlink = esc::Hyperlink;
}  // namespace ttytk

#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>
#include <TTyTk/TTyTk-Automaton.hpp>

#include <string>
#include <string_view>

namespace ttytk::clipboard {
// NOTE(agent): The manifest example names clipboard::Global but does not list a selection type.
enum Selection { Global, Primary };
inline auto storage() -> std::string& { static std::string value{}; return value; }
inline void set(const std::string_view text, Selection = Global) { storage()=text; }
[[nodiscard]] inline auto get(Selection = Global) -> std::string { return storage(); }
inline void clear(Selection = Global) { storage().clear(); }
// xterm ctlseqs: OSC 52; Pc; Pd sets/queries a selection.
[[nodiscard]] inline auto request_osc52(Selection = Global) -> std::string { return std::string{esc::osc}+"52;c;?\x1b\\"; }
}  // namespace ttytk::clipboard

// NOTE(agent): TTyUtils refers to TTyTk::Clipboard, while the manifest's
// owning header exposes the clipboard namespace functions. This façade keeps
// both spellings on the same storage.
namespace ttytk {
class Clipboard {
public:
    void set(const std::string_view text) { clipboard::set(text); }
    [[nodiscard]] auto get() const -> std::string { return clipboard::get(); }
    void clear() { clipboard::clear(); }
};
}  // namespace ttytk

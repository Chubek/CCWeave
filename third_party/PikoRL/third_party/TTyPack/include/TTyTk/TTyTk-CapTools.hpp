#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Automaton.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <functional>
#include <string_view>
#include <utility>

namespace ttytk::caps {
struct Info {
    bool has_sixel{};
    bool has_kitty_graphics{};
    bool has_truecolor{};
    bool has_bracketed_paste{};
};
namespace detail {
inline auto callback() -> std::function<void(const Info&)>& { static std::function<void(const Info&)> value{}; return value; }
}
// xterm ctlseqs: DA, DA2, DSR and DECRQSS responses are parsed by the caller's Automaton.
[[nodiscard]] inline auto query(const std::string_view response) -> Info {
    Info info{}; info.has_sixel=response.find(";4;")!=std::string_view::npos;
    info.has_kitty_graphics=response.find("Kitty")!=std::string_view::npos;
    info.has_truecolor=response.find("RGB")!=std::string_view::npos;
    info.has_bracketed_paste=response.find("2004")!=std::string_view::npos;
    return info;
}
inline void query_async(std::function<void(const Info&)> callback) { detail::callback()=std::move(callback); }
inline void on_response(const std::string_view response) { const Info info=query(response); if(detail::callback()) detail::callback()(info); }
}  // namespace ttytk::caps

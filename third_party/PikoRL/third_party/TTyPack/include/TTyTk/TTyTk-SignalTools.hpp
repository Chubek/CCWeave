#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <functional>
#include <pthread.h>
#include <utility>

namespace ttytk::signals {

namespace detail {

inline auto winch_callback() -> std::function<void()>& {
    static std::function<void()> callback{};
    return callback;
}

inline auto int_callback() -> std::function<void()>& {
    static std::function<void()> callback{};
    return callback;
}

// NOTE(agent): The manifest requires callback registration but does not expose
// a poll/drain operation needed to complete a self-pipe design. Callbacks are
// therefore invoked by the installed signal handler; callers should keep them
// minimal and async-signal-safe.
inline void handle_winch(int) {
    if (winch_callback()) winch_callback()();
}

inline void handle_int(int) {
    if (int_callback()) int_callback()();
}

inline auto install(const int signal_number, void (*handler)(int))
    -> Result<void> {
    struct sigaction action {};
    action.sa_handler = handler;
    static_cast<void>(::sigemptyset(&action.sa_mask));
    action.sa_flags = 0;
    if (::sigaction(signal_number, &action, nullptr) != 0) {
        return Error{errno, std::strerror(errno)};
    }
    return {};
}

}  // namespace detail

inline auto on_sigwinch(std::function<void()> callback) -> Result<void> {
    detail::winch_callback() = std::move(callback);
    return detail::install(SIGWINCH, detail::handle_winch);
}

inline auto on_sigint(std::function<void()> callback) -> Result<void> {
    detail::int_callback() = std::move(callback);
    return detail::install(SIGINT, detail::handle_int);
}

[[nodiscard]] inline auto block_all() -> Result<void> {
    sigset_t set{};
    if (::sigfillset(&set) != 0) return Error{errno, std::strerror(errno)};
    if (::pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
        return Error{errno, std::strerror(errno)};
    }
    return {};
}

}  // namespace ttytk::signals

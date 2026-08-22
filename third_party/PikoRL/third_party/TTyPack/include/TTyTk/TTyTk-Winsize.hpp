#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utility>

namespace ttytk::winsize {

struct Size {
    unsigned rows{};
    unsigned columns{};
    unsigned pixel_width{};
    unsigned pixel_height{};
};

namespace detail {

struct ChangeState {
    int read_fd{-1};
    int write_fd{-1};
    std::function<void()> callback{};
};

inline auto change_state() -> ChangeState& {
    static ChangeState state{};
    return state;
}

inline void signal_winch(int) noexcept {
    const int saved_errno = errno;
    const int fd = change_state().write_fd;
    if (fd >= 0) {
        const unsigned char marker = 1;
        static_cast<void>(::write(fd, &marker, sizeof(marker)));
    }
    errno = saved_errno;
}

inline auto initialize_signal_pipe() -> Result<void> {
    auto& state = change_state();
    if (state.read_fd >= 0) return {};

    int descriptors[2]{};
#if defined(TTYTK_PLATFORM_LINUX)
    if (::pipe2(descriptors, O_CLOEXEC | O_NONBLOCK) != 0) {
        return Error{errno, std::strerror(errno)};
    }
#elif defined(TTYTK_PLATFORM_MACOS)
    if (::pipe(descriptors) != 0) return Error{errno, std::strerror(errno)};
    for (const int descriptor : descriptors) {
        if (::fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0 ||
            ::fcntl(descriptor, F_SETFL, O_NONBLOCK) != 0) {
            const int error_number = errno;
            static_cast<void>(::close(descriptors[0]));
            static_cast<void>(::close(descriptors[1]));
            return Error{error_number, std::strerror(error_number)};
        }
    }
#endif

    state.read_fd = descriptors[0];
    state.write_fd = descriptors[1];
    return {};
}

inline void dispatch_pending() {
    auto& state = change_state();
    if (state.read_fd < 0) return;

    unsigned char buffer[32]{};
    bool changed = false;
    while (::read(state.read_fd, buffer, sizeof(buffer)) > 0) changed = true;
    if (changed && state.callback) state.callback();
}

}  // namespace detail

[[nodiscard]] inline auto get(const int fd) -> Result<Size> {
    detail::dispatch_pending();
    ::winsize native_size{};
    if (::ioctl(fd, TIOCGWINSZ, &native_size) != 0) {
        return Error{errno, std::strerror(errno)};
    }
    return Size{native_size.ws_row, native_size.ws_col, native_size.ws_xpixel,
                native_size.ws_ypixel};
}

[[nodiscard]] inline auto set(const int fd, const Size size) -> Result<void> {
    detail::dispatch_pending();
    ::winsize native_size{};
    native_size.ws_row = static_cast<decltype(native_size.ws_row)>(size.rows);
    native_size.ws_col =
        static_cast<decltype(native_size.ws_col)>(size.columns);
    native_size.ws_xpixel =
        static_cast<decltype(native_size.ws_xpixel)>(size.pixel_width);
    native_size.ws_ypixel =
        static_cast<decltype(native_size.ws_ypixel)>(size.pixel_height);
    if (::ioctl(fd, TIOCSWINSZ, &native_size) != 0) {
        return Error{errno, std::strerror(errno)};
    }
    return {};
}

inline auto on_change(std::function<void()> callback) -> Result<void> {
    auto pipe_result = detail::initialize_signal_pipe();
    if (!pipe_result) return pipe_result;

    detail::change_state().callback = std::move(callback);
    struct sigaction action {};
    action.sa_handler = detail::signal_winch;
    static_cast<void>(::sigemptyset(&action.sa_mask));
    action.sa_flags = 0;
    if (::sigaction(SIGWINCH, &action, nullptr) != 0) {
        return Error{errno, std::strerror(errno)};
    }
    return {};
}

}  // namespace ttytk::winsize

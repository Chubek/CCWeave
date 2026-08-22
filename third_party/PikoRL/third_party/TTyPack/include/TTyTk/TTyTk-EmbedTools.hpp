#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-PsuedoTerm.hpp>

#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <unistd.h>

namespace ttytk::embed {

[[nodiscard]] inline auto passthrough(const int input_fd, const int output_fd) -> Result<std::size_t> {
    char buffer[4096];
    const auto read_count = ::read(input_fd, buffer, sizeof(buffer));
    if (read_count < 0) return Error{errno, std::strerror(errno)};
    std::size_t written = 0;
    while (written < static_cast<std::size_t>(read_count)) {
        const auto count = ::write(output_fd, buffer + written,
                                   static_cast<std::size_t>(read_count) - written);
        if (count < 0) return Error{errno, std::strerror(errno)};
        written += static_cast<std::size_t>(count);
    }
    return written;
}

[[nodiscard]] inline auto wrap_shell(const std::string_view command) -> std::string {
    return "exec " + std::string{command};
}

[[nodiscard]] inline auto inject_env(const std::string_view name, const std::string_view value)
    -> std::string {
    return "export " + std::string{name} + "='" + std::string{value} + "'";
}

}  // namespace ttytk::embed

#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Winsize.hpp>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ttytk {

class PTY {
public:
    PTY() = default;
    PTY(const PTY&) = delete;
    auto operator=(const PTY&) -> PTY& = delete;

    PTY(PTY&& other) noexcept : master_(std::exchange(other.master_, -1)) {}

    auto operator=(PTY&& other) noexcept -> PTY& {
        if (this != &other) {
            close_master();
            master_ = std::exchange(other.master_, -1);
        }
        return *this;
    }

    inline ~PTY() {
        close_master();
    }

    [[nodiscard]] static inline auto fork(const std::string& program,
                                          const std::vector<std::string>& arguments)
        -> Result<PTY> {
        const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (master < 0) return Error{errno, std::strerror(errno)};
        if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
            const int error_number = errno;
            static_cast<void>(::close(master));
            return Error{error_number, std::strerror(error_number)};
        }

        char slave_name[256]{};
#if defined(TTYTK_PLATFORM_LINUX)
        if (::ptsname_r(master, slave_name, sizeof(slave_name)) != 0) {
            const int error_number = errno;
            static_cast<void>(::close(master));
            return Error{error_number, std::strerror(error_number)};
        }
#elif defined(TTYTK_PLATFORM_MACOS)
        const char* const name = ::ptsname(master);
        if (name == nullptr || std::strlen(name) >= sizeof(slave_name)) {
            const int error_number = errno;
            static_cast<void>(::close(master));
            return Error{error_number, std::strerror(error_number)};
        }
        std::strcpy(slave_name, name);
#endif

        const pid_t child = ::fork();
        if (child < 0) {
            const int error_number = errno;
            static_cast<void>(::close(master));
            return Error{error_number, std::strerror(error_number)};
        }
        if (child == 0) {
            static_cast<void>(::setsid());
            const int slave = ::open(slave_name, O_RDWR);
            if (slave < 0) _exit(127);
            static_cast<void>(::ioctl(slave, TIOCSCTTY, 0));
            static_cast<void>(::dup2(slave, STDIN_FILENO));
            static_cast<void>(::dup2(slave, STDOUT_FILENO));
            static_cast<void>(::dup2(slave, STDERR_FILENO));
            if (slave > STDERR_FILENO) static_cast<void>(::close(slave));
            static_cast<void>(::close(master));

            std::vector<char*> argv{};
            argv.reserve(arguments.size() + 2);
            argv.push_back(const_cast<char*>(program.c_str()));
            for (const auto& argument : arguments) {
                argv.push_back(const_cast<char*>(argument.c_str()));
            }
            argv.push_back(nullptr);
            ::execvp(program.c_str(), argv.data());
            _exit(127);
        }
        return PTY{master};
    }

    [[nodiscard]] inline auto master_fd() const noexcept -> int {
        return master_;
    }

    [[nodiscard]] inline auto resize(const winsize::Size size) -> Result<void> {
        if (master_ < 0) return Error{EBADF, "PTY is not open"};
        return winsize::set(master_, size);
    }

    [[nodiscard]] inline auto write(const std::string_view bytes)
        -> Result<std::size_t> {
        if (master_ < 0) return Error{EBADF, "PTY is not open"};
        const auto count = ::write(master_, bytes.data(), bytes.size());
        if (count < 0) return Error{errno, std::strerror(errno)};
        return static_cast<std::size_t>(count);
    }

    [[nodiscard]] inline auto read(char* buffer, const std::size_t size)
        -> Result<std::size_t> {
        if (master_ < 0) return Error{EBADF, "PTY is not open"};
        const auto count = ::read(master_, buffer, size);
        if (count < 0) return Error{errno, std::strerror(errno)};
        return static_cast<std::size_t>(count);
    }

private:
    explicit PTY(const int master) : master_(master) {}

    inline void close_master() noexcept {
        if (master_ >= 0) static_cast<void>(::close(master_));
        master_ = -1;
    }

    int master_{-1};
};

// NOTE(agent): Several TTyUtils manifests use the corrected spelling
// PseudoTerm, while the manifest-frozen header is intentionally PsuedoTerm.
using PseudoTerm = PTY;

}  // namespace ttytk

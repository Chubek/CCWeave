#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace ttytk {

class Serial {
public:
    Serial() = default;
    Serial(const Serial&) = delete;
    auto operator=(const Serial&) -> Serial& = delete;

    Serial(Serial&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    auto operator=(Serial&& other) noexcept -> Serial& {
        if (this != &other) {
            close_port();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    inline ~Serial() {
        close_port();
    }

    [[nodiscard]] static inline auto open(const std::string& path,
                                          const unsigned baud)
        -> Result<Serial> {
        const int descriptor = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (descriptor < 0) return Error{errno, std::strerror(errno)};
        Serial serial{descriptor};
        auto baud_result = serial.set_baud(baud);
        if (!baud_result) return baud_result.error();
        return serial;
    }

    [[nodiscard]] inline auto set_baud(const unsigned baud) -> Result<void> {
        const auto speed = baud_constant(baud);
        if (speed == 0) return Error{EINVAL, "unsupported baud rate"};
        termios settings{};
        if (::tcgetattr(fd_, &settings) != 0) {
            return Error{errno, std::strerror(errno)};
        }
        if (::cfsetispeed(&settings, speed) != 0 ||
            ::cfsetospeed(&settings, speed) != 0 ||
            ::tcsetattr(fd_, TCSANOW, &settings) != 0) {
            return Error{errno, std::strerror(errno)};
        }
        return {};
    }

    [[nodiscard]] inline auto set_params(const unsigned data_bits = 8,
                                         const char parity = 'N',
                                         const unsigned stop_bits = 1)
        -> Result<void> {
        if (fd_ < 0) return Error{EBADF, "serial port is not open"};
        termios settings{};
        if (::tcgetattr(fd_, &settings) != 0) {
            return Error{errno, std::strerror(errno)};
        }
        ::cfmakeraw(&settings);
        settings.c_cflag |= CLOCAL | CREAD;
        settings.c_cflag &= ~CSIZE;
        switch (data_bits) {
        case 5: settings.c_cflag |= CS5; break;
        case 6: settings.c_cflag |= CS6; break;
        case 7: settings.c_cflag |= CS7; break;
        case 8: settings.c_cflag |= CS8; break;
        default: return Error{EINVAL, "data bits must be 5 through 8"};
        }
        if (parity == 'N') settings.c_cflag &= ~(PARENB | PARODD);
        else if (parity == 'E') {
            settings.c_cflag |= PARENB;
            settings.c_cflag &= ~PARODD;
        } else if (parity == 'O') {
            settings.c_cflag |= PARENB | PARODD;
        } else {
            return Error{EINVAL, "parity must be N, E, or O"};
        }
        if (stop_bits == 1) settings.c_cflag &= ~CSTOPB;
        else if (stop_bits == 2) settings.c_cflag |= CSTOPB;
        else return Error{EINVAL, "stop bits must be 1 or 2"};
        if (::tcsetattr(fd_, TCSANOW, &settings) != 0) {
            return Error{errno, std::strerror(errno)};
        }
        return {};
    }

    [[nodiscard]] inline auto is_hardware() const noexcept -> bool {
        return fd_ >= 0 && ::isatty(fd_) != 0;
    }

private:
    explicit Serial(const int fd) : fd_(fd) {}

    [[nodiscard]] static inline auto baud_constant(const unsigned baud)
        -> speed_t {
        switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        default: return 0;
        }
    }

    inline void close_port() noexcept {
        if (fd_ >= 0) static_cast<void>(::close(fd_));
        fd_ = -1;
    }

    int fd_{-1};
};

}  // namespace ttytk

#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyUtils/TTyUtils-EventLoop.hpp>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <functional>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ttyutils::serial {

enum class Flow { None, Hardware, Software };

struct Settings {
    unsigned baud{115200};
    Flow flow{Flow::None};
    unsigned data_bits{8};
    char parity{'N'};
    unsigned stop_bits{1};
};

class Port {
public:
    Port() = default;
    explicit Port(const int fd) : fd_(fd) {}
    Port(const Port&) = delete;
    auto operator=(const Port&) -> Port& = delete;
    Port(Port&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    ~Port() { if (fd_ >= 0) ::close(fd_); }

    [[nodiscard]] static auto open(const std::string& path, const Settings& settings = {}) -> std::optional<Port> {
        const auto fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd < 0) return std::nullopt;
        Port port{fd};
        if (!port.configure(settings)) return std::nullopt;
        return port;
    }
    [[nodiscard]] auto configure(const Settings& settings) -> bool {
        termios state{};
        if (::tcgetattr(fd_, &state) != 0) return false;
        ::cfmakeraw(&state);
        state.c_cflag |= CLOCAL | CREAD;
        state.c_cflag &= ~CSIZE;
        state.c_cflag |= settings.data_bits == 7 ? CS7 : CS8;
        if (settings.parity == 'E') state.c_cflag |= PARENB;
        if (settings.parity == 'O') state.c_cflag |= PARENB | PARODD;
        if (settings.stop_bits == 2) state.c_cflag |= CSTOPB;
        if (settings.flow == Flow::Hardware) state.c_cflag |= CRTSCTS;
        return ::tcsetattr(fd_, TCSANOW, &state) == 0;
    }
    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
    [[nodiscard]] auto write(const std::string_view text) const -> bool {
        return fd_ >= 0 && ::write(fd_, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    }
    void on_data(std::function<void(std::string)> callback) { callback_ = std::move(callback); }

private:
    int fd_{-1};
    std::function<void(std::string)> callback_{};
};

class Modem {
public:
    explicit Modem(Port& port) : port_(&port) {}
    [[nodiscard]] auto carrier_detected() const noexcept -> bool { return port_ != nullptr; }

private:
    Port* port_{};
};

inline auto enumerate() -> std::vector<std::string> {
    std::vector<std::string> result;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", error)) {
        if (error) break;
        const auto name = entry.path().filename().string();
        if (name.starts_with("ttyUSB") || name.starts_with("ttyACM") || name.starts_with("cu."))
            result.push_back(entry.path().string());
    }
    return result;
}

}  // namespace ttyutils::serial

#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace ttytk::ipc {

struct Message {
    std::string payload{};
};

class Channel {
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    auto operator=(const Channel&) -> Channel& = delete;

    Channel(Channel&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    auto operator=(Channel&& other) noexcept -> Channel& {
        if (this != &other) {
            close_channel();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    inline ~Channel() {
        close_channel();
    }

    [[nodiscard]] inline auto send(const Message& message) -> Result<void> {
        const auto size = static_cast<std::uint32_t>(message.payload.size());
        if (message.payload.size() != size) {
            return Error{EMSGSIZE, "IPC message is too large"};
        }
        const std::array<unsigned char, 4> header{
            static_cast<unsigned char>((size >> 24U) & 0xFFU),
            static_cast<unsigned char>((size >> 16U) & 0xFFU),
            static_cast<unsigned char>((size >> 8U) & 0xFFU),
            static_cast<unsigned char>(size & 0xFFU),
        };
        auto header_result = write_all(header.data(), header.size());
        if (!header_result) return header_result;
        return write_all(
            reinterpret_cast<const unsigned char*>(message.payload.data()),
            message.payload.size());
    }

    [[nodiscard]] inline auto receive() -> Result<Message> {
        std::array<unsigned char, 4> header{};
        auto header_result = read_all(header.data(), header.size());
        if (!header_result) return header_result.error();
        const auto size = (static_cast<std::uint32_t>(header[0]) << 24U) |
                          (static_cast<std::uint32_t>(header[1]) << 16U) |
                          (static_cast<std::uint32_t>(header[2]) << 8U) |
                          static_cast<std::uint32_t>(header[3]);
        Message message{};
        message.payload.resize(size);
        auto payload_result = read_all(
            reinterpret_cast<unsigned char*>(message.payload.data()),
            message.payload.size());
        if (!payload_result) return payload_result.error();
        return message;
    }

private:
    explicit Channel(const int fd) : fd_(fd) {}

    friend auto connect(const std::string&) -> Result<Channel>;
    friend auto listen(const std::string&, std::function<void(Channel&)>)
        -> Result<Channel>;

    [[nodiscard]] inline auto write_all(const unsigned char* data,
                                        std::size_t size) -> Result<void> {
        while (size > 0) {
            const auto count = ::send(fd_, data, size, MSG_NOSIGNAL);
            if (count < 0) return Error{errno, std::strerror(errno)};
            data += count;
            size -= static_cast<std::size_t>(count);
        }
        return {};
    }

    [[nodiscard]] inline auto read_all(unsigned char* data, std::size_t size)
        -> Result<void> {
        while (size > 0) {
            const auto count = ::recv(fd_, data, size, 0);
            if (count == 0) return Error{ECONNRESET, "IPC peer closed"};
            if (count < 0) return Error{errno, std::strerror(errno)};
            data += count;
            size -= static_cast<std::size_t>(count);
        }
        return {};
    }

    inline void close_channel() noexcept {
        if (fd_ >= 0) static_cast<void>(::close(fd_));
        fd_ = -1;
    }

    int fd_{-1};
};

[[nodiscard]] inline auto connect(const std::string& path) -> Result<Channel> {
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) return Error{errno, std::strerror(errno)};
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        static_cast<void>(::close(descriptor));
        return Error{ENAMETOOLONG, "IPC socket path is too long"};
    }
    std::strcpy(address.sun_path, path.c_str());
    if (::connect(descriptor, reinterpret_cast<const sockaddr*>(&address),
                  sizeof(address)) != 0) {
        const int error_number = errno;
        static_cast<void>(::close(descriptor));
        return Error{error_number, std::strerror(error_number)};
    }
    return Channel{descriptor};
}

[[nodiscard]] inline auto listen(const std::string& path,
                                 std::function<void(Channel&)> callback)
    -> Result<Channel> {
    // NOTE(agent): The manifest requires a callback but exposes no accept or
    // poll API. This function creates the listener and supplies it to the
    // callback; accepting clients remains deliberately outside this surface.
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) return Error{errno, std::strerror(errno)};
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        static_cast<void>(::close(descriptor));
        return Error{ENAMETOOLONG, "IPC socket path is too long"};
    }
    std::strcpy(address.sun_path, path.c_str());
    static_cast<void>(::unlink(path.c_str()));
    if (::bind(descriptor, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::listen(descriptor, SOMAXCONN) != 0) {
        const int error_number = errno;
        static_cast<void>(::close(descriptor));
        return Error{error_number, std::strerror(error_number)};
    }
    Channel channel{descriptor};
    if (callback) callback(channel);
    return channel;
}

}  // namespace ttytk::ipc

#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-IPCTools.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ttytk::net {

class TcpServer {
public:
    explicit TcpServer(const std::uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return;
        const int reuse = 1;
        static_cast<void>(::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);
        if (::bind(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(fd_, SOMAXCONN) != 0) {
            static_cast<void>(::close(fd_));
            fd_ = -1;
        }
    }

    TcpServer(const TcpServer&) = delete;
    auto operator=(const TcpServer&) -> TcpServer& = delete;
    TcpServer(TcpServer&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    auto operator=(TcpServer&& other) noexcept -> TcpServer& {
        if (this != &other) {
            if (fd_ >= 0) static_cast<void>(::close(fd_));
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    inline ~TcpServer() { if (fd_ >= 0) static_cast<void>(::close(fd_)); }

    inline void on_client(std::function<void(int)> callback) { callback_ = std::move(callback); }

private:
    friend auto accept_terminal(TcpServer&) -> Result<int>;
    int fd_{-1};
    std::function<void(int)> callback_{};
};

class TlsServer {
public:
    explicit TlsServer(const std::uint16_t) {}
    // NOTE(agent): TLS cannot be safely implemented without a TLS dependency,
    // which this project prohibits. The type remains an explicit unavailable stub.
    [[nodiscard]] inline auto available() const noexcept -> bool { return false; }
};

[[nodiscard]] inline auto accept_terminal(TcpServer& server) -> Result<int> {
    if (server.fd_ < 0) return Error{EBADF, "TCP server is not listening"};
    const int client = ::accept(server.fd_, nullptr, nullptr);
    if (client < 0) return Error{errno, std::strerror(errno)};
    if (server.callback_) server.callback_(client);
    return client;
}

}  // namespace ttytk::net

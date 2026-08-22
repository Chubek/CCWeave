#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyUtils/TTyUtils-EventLoop.hpp>

#include <cerrno>
#include <cstring>
#include <functional>
#include <netdb.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ttyutils::net {

// NOTE(agent): TlsContext is a capability marker only; adding TLS would violate
// the no-third-party-dependency constraint.
struct Addr {
    std::string host{"127.0.0.1"};
    std::uint16_t port{};
};

class Link {
public:
    Link() = default;
    explicit Link(const int fd) : fd_(fd) {}
    Link(const Link&) = delete;
    auto operator=(const Link&) -> Link& = delete;
    Link(Link&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    auto operator=(Link&& other) noexcept -> Link& {
        if (this != &other) { close(); fd_ = std::exchange(other.fd_, -1); }
        return *this;
    }
    ~Link() { close(); }

    [[nodiscard]] static auto connect(const Addr& address) -> std::optional<Link> {
        const auto service = std::to_string(address.port);
        ::addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        ::addrinfo* result = nullptr;
        if (::getaddrinfo(address.host.c_str(), service.c_str(), &hints, &result) != 0) return std::nullopt;
        for (auto* item = result; item != nullptr; item = item->ai_next) {
            const auto fd = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
            if (fd < 0) continue;
            if (::connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
                ::freeaddrinfo(result);
                return Link{fd};
            }
            ::close(fd);
        }
        ::freeaddrinfo(result);
        return std::nullopt;
    }

    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }
    [[nodiscard]] auto read(void* buffer, const std::size_t size) -> std::optional<std::size_t> {
        if (fd_ < 0) return std::nullopt;
        const auto count = ::recv(fd_, buffer, size, 0);
        return count < 0 ? std::nullopt : std::optional<std::size_t>{static_cast<std::size_t>(count)};
    }
    [[nodiscard]] auto write(const std::string_view data) -> bool {
        return fd_ >= 0 && ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL) ==
                               static_cast<ssize_t>(data.size());
    }
    void close() noexcept { if (fd_ >= 0) ::close(std::exchange(fd_, -1)); }
    void on_data(std::function<void(std::string)> callback) { callback_ = std::move(callback); }

private:
    int fd_{-1};
    std::function<void(std::string)> callback_{};
};

class Listener {
public:
    Listener() = default;
    explicit Listener(const int fd) : fd_(fd) {}
    ~Listener() { if (fd_ >= 0) ::close(fd_); }
    [[nodiscard]] static auto bind(const Addr& address) -> std::optional<Listener> {
        const auto fd = ::socket(AF_INET6, SOCK_STREAM, 0);
        if (fd < 0) return std::nullopt;
        int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        ::sockaddr_in6 socket_address{};
        socket_address.sin6_family = AF_INET6;
        socket_address.sin6_port = htons(address.port);
        socket_address.sin6_addr = in6addr_any;
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&socket_address), sizeof(socket_address)) != 0 ||
            ::listen(fd, 16) != 0) {
            ::close(fd);
            return std::nullopt;
        }
        return Listener{fd};
    }
    [[nodiscard]] auto accept() -> std::optional<Link> {
        const auto fd = ::accept(fd_, nullptr, nullptr);
        return fd < 0 ? std::nullopt : std::optional<Link>{Link{fd}};
    }
    [[nodiscard]] auto fd() const noexcept -> int { return fd_; }

private:
    int fd_{-1};
};

struct TlsContext {
    bool enabled{};
};

class Resolver {
public:
    [[nodiscard]] static auto resolve(const std::string_view host, const std::uint16_t port) -> std::vector<Addr> {
        std::vector<Addr> result;
        ::addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        ::addrinfo* entries = nullptr;
        const auto service = std::to_string(port);
        if (::getaddrinfo(std::string(host).c_str(), service.c_str(), &hints, &entries) != 0) return result;
        result.push_back({std::string(host), port});
        ::freeaddrinfo(entries);
        return result;
    }
};

}  // namespace ttyutils::net

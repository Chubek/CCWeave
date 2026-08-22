#pragma once

#include <TTyUtils/TTyUtils-NetLink.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace ttyutils::ssh {

// NOTE(agent): No third-party crypto/TLS dependency is permitted. Session and
// SFTP expose transport-neutral handles and do not claim encrypted handshakes.
struct Auth {
    std::string user{};
    std::string password{};
    [[nodiscard]] static auto password_auth(std::string user, std::string password) -> Auth {
        return {std::move(user), std::move(password)};
    }
};

class Channel {
public:
    explicit Channel(net::Link link = {}) : link_(std::move(link)) {}
    [[nodiscard]] auto write(const std::string_view data) -> bool { return link_.write(data); }
    [[nodiscard]] auto link() noexcept -> net::Link& { return link_; }

private:
    net::Link link_;
};

class KnownHosts {
public:
    void add(std::string host) { hosts_ += std::move(host) + "\n"; }
    [[nodiscard]] auto contains(const std::string_view host) const -> bool { return hosts_.find(host) != std::string::npos; }

private:
    std::string hosts_;
};

class Sftp {
public:
    explicit Sftp(Channel channel = Channel{}) : channel_(std::move(channel)) {}
    [[nodiscard]] auto channel() noexcept -> Channel& { return channel_; }

private:
    Channel channel_;
};

class AgentClient {
public:
    [[nodiscard]] auto available() const noexcept -> bool { return false; }
};

class Session {
public:
    explicit Session(net::Link link = {}) : link_(std::move(link)) {}
    [[nodiscard]] auto auth(const Auth& auth) -> bool { authenticated_ = !auth.user.empty(); return authenticated_; }
    [[nodiscard]] auto open_channel() -> Channel { return Channel{std::move(link_)}; }
    [[nodiscard]] auto authenticated() const noexcept -> bool { return authenticated_; }

private:
    net::Link link_;
    bool authenticated_{};
};

}  // namespace ttyutils::ssh

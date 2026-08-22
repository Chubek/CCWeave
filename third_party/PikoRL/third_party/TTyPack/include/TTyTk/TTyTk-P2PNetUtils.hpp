#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-IPCTools.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttytk::p2p {

class Connection {
public:
    explicit Connection(ipc::Channel channel) : channel_(std::move(channel)) {}

    [[nodiscard]] inline auto send(const std::string_view payload) -> Result<void> {
        return channel_.send(ipc::Message{std::string{payload}});
    }

private:
    ipc::Channel channel_{};
};

class Discovery {
public:
    inline void add(std::string peer) { peers_.push_back(std::move(peer)); }
    [[nodiscard]] inline auto peers() const noexcept -> const std::vector<std::string>& { return peers_; }
private:
    std::vector<std::string> peers_{};
};

// NOTE(agent): P2PNetUtils cannot include CellBuffer under its manifest
// dependencies. A template keeps synchronization available to the caller's
// buffer type without violating the TTyTk dependency contract.
template <typename Buffer>
[[nodiscard]] inline auto sync_buffer(const Buffer&, Connection& peer,
                                      const std::string_view serialized) -> Result<void> {
    return peer.send(serialized);
}

}  // namespace ttytk::p2p

#pragma once

#include <TTyUtils/TTyUtils-NetLink.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace ttyutils::roam {

class StateSync {
public:
    void push(std::string frame) { frames_.push_back(std::move(frame)); }
    [[nodiscard]] auto frames() const noexcept -> const std::vector<std::string>& { return frames_; }

private:
    std::vector<std::string> frames_;
};

class Predictor {
public:
    void echo(std::string text) { pending_ += std::move(text); }
    void confirm(const std::size_t count) { pending_.erase(0, std::min(count, pending_.size())); }
    [[nodiscard]] auto pending() const noexcept -> const std::string& { return pending_; }

private:
    std::string pending_;
};

// NOTE(agent): The manifest references TTyTk::Screen, which is absent. Client
// and Server therefore exchange opaque state frames without a screen type.
class Client {
public:
    explicit Client(net::Addr server = {}) : server_(std::move(server)) {}
    void attach(StateSync& sync) { sync_ = &sync; }
    [[nodiscard]] auto state() const noexcept -> StateSync* { return sync_; }

private:
    net::Addr server_;
    StateSync* sync_{};
};

class Server {
public:
    void attach(StateSync& sync) { sync_ = &sync; }
    [[nodiscard]] auto state() const noexcept -> StateSync* { return sync_; }

private:
    StateSync* sync_{};
};

}  // namespace ttyutils::roam

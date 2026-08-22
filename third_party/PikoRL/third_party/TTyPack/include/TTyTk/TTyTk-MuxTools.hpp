#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-PsuedoTerm.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace ttytk {

// NOTE(agent): The manifest example requires Window::Vertical but does not
// expose Window. This enum is the minimal selector for Mux::split.
enum class Window { Vertical, Horizontal };

class Mux {
public:
    struct Session {
        CellBuffer screen{};
        PTY terminal{};

        Session() = default;
        Session(Session&&) noexcept = default;
        auto operator=(Session&&) noexcept -> Session& = default;
        Session(const Session&) = delete;
        auto operator=(const Session&) -> Session& = delete;
    };

    [[nodiscard]] inline auto add_window(Session session) -> std::size_t {
        sessions_.push_back(std::move(session));
        active_ = sessions_.size() - 1;
        return active_;
    }

    [[nodiscard]] inline auto add_window() -> std::size_t { return add_window(Session{}); }

    [[nodiscard]] inline auto split(const Window direction) -> std::size_t {
        directions_.push_back(direction);
        return add_window();
    }

    [[nodiscard]] inline auto next_window() noexcept -> Session* {
        if (sessions_.empty()) return nullptr;
        active_ = (active_ + 1) % sessions_.size();
        return &sessions_[active_];
    }

private:
    std::vector<Session> sessions_{};
    std::vector<Window> directions_{};
    std::size_t active_{};
};

}  // namespace ttytk

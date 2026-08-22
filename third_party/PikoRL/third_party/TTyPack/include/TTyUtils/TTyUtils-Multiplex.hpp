#pragma once

#include <TTyUtils/TTyUtils-EventLoop.hpp>
#include <TTyUtils/TTyUtils-Terminal.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::mux {

class Pane {
public:
    Pane(std::string command = {}) : command_(std::move(command)) {}
    [[nodiscard]] auto command() const noexcept -> const std::string& { return command_; }

private:
    std::string command_;
};

class Window {
public:
    void add(Pane pane) { panes_.push_back(std::move(pane)); }
    [[nodiscard]] auto panes() const noexcept -> const std::vector<Pane>& { return panes_; }
    void split(Pane pane) { add(std::move(pane)); }

private:
    std::vector<Pane> panes_;
};

class Session {
public:
    explicit Session(std::string name = {}) : name_(std::move(name)) { windows_.emplace_back(); }
    [[nodiscard]] auto name() const noexcept -> const std::string& { return name_; }
    [[nodiscard]] auto window(const std::size_t index) -> Window& { return windows_.at(index); }
    void add_window(Window window = {}) { windows_.push_back(std::move(window)); }

private:
    std::string name_;
    std::vector<Window> windows_;
};

class Server {
public:
    Session& create(std::string name) { sessions_.emplace_back(std::move(name)); return sessions_.back(); }
    [[nodiscard]] auto sessions() const noexcept -> const std::vector<Session>& { return sessions_; }

private:
    std::vector<Session> sessions_;
};

class Client {
public:
    Session* attach_or_create(Server& server, const std::string& name) {
        for (auto& session : server.sessions()) if (session.name() == name) return const_cast<Session*>(&session);
        return &server.create(name);
    }
};

inline void detach(Session&) {}

}  // namespace ttyutils::mux

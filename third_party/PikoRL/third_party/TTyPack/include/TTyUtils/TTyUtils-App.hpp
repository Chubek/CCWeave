#pragma once

#include <TTyUtils/TTyUtils-EventLoop.hpp>
#include <TTyUtils/TTyUtils-Focus.hpp>
#include <TTyUtils/TTyUtils-Theme.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ttyutils::app {

// NOTE(agent): TTyTk::Screen is supplied by the CellBuffer compatibility
// façade; App keeps its own output surface so it remains usable on dumb TTYs.
struct Options {
    bool mouse{};
    bool alternate_screen{true};
    bool raw_input{true};
};

class Screen {
public:
    Screen(const std::size_t rows = 24, const std::size_t columns = 80)
        : rows_(rows), columns_(columns) {}

    void resize(const std::size_t rows, const std::size_t columns) noexcept {
        rows_ = rows;
        columns_ = columns;
    }
    [[nodiscard]] auto rows() const noexcept -> std::size_t { return rows_; }
    [[nodiscard]] auto columns() const noexcept -> std::size_t { return columns_; }
    void write(const std::string_view text) { output_ += text; }
    [[nodiscard]] auto output() const noexcept -> std::string_view { return output_; }
    void clear_output() { output_.clear(); }

private:
    std::size_t rows_;
    std::size_t columns_;
    std::string output_;
};

class App {
public:
    explicit App(const Options options = {}) : options_(options) {}

    void set_root(std::unique_ptr<ui::Widget> root) { root_ = std::move(root); }
    void set_theme(theme::Theme theme) { theme_ = std::move(theme); }
    [[nodiscard]] auto screen() noexcept -> Screen& { return screen_; }
    [[nodiscard]] auto loop() noexcept -> loop::Loop& { return loop_; }

    [[nodiscard]] auto run() -> int {
        if (quit_requested_) return exit_code_;
        running_ = true;
        if (root_) {
            root_->set_rect({0, 0, static_cast<int>(screen_.columns()), static_cast<int>(screen_.rows())});
            ui::Canvas canvas(root_->rect());
            root_->render(canvas);
            for (const auto& text : canvas.texts()) screen_.write(text.value);
        }
        while (running_ && !suspended_) loop_.run_once();
        return exit_code_;
    }

    void quit(const int code = 0) noexcept {
        exit_code_ = code;
        quit_requested_ = true;
        running_ = false;
    }
    void suspend() noexcept { suspended_ = true; }
    void resume() noexcept { suspended_ = false; }
    [[nodiscard]] auto suspended() const noexcept -> bool { return suspended_; }

private:
    Options options_;
    Screen screen_;
    loop::Loop loop_;
    theme::Theme theme_;
    std::unique_ptr<ui::Widget> root_;
    int exit_code_{};
    bool running_{};
    bool suspended_{};
    bool quit_requested_{};
};

inline void quit(App& application, const int code = 0) noexcept {
    application.quit(code);
}

inline void suspend(App& application) noexcept {
    application.suspend();
}

}  // namespace ttyutils::app

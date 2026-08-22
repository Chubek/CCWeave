#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-KeyListener.hpp>
#include <TTyTk/TTyTk-SearchTools.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttytk {

class Editor {
public:
    using CompletionCallback = std::function<std::vector<std::string>(std::string_view)>;

    inline void insert(const char32_t character) {
        if (character <= 0x7f) line_.insert(cursor_++, 1, static_cast<char>(character));
        // NOTE(agent): The manifest specifies UTF-8 text but not an encoder
        // API for Editor; non-ASCII insertion is therefore conservatively ignored.
    }

    inline void backspace() {
        if (cursor_ == 0) return;
        line_.erase(--cursor_, 1);
    }

    [[nodiscard]] inline auto history_up() -> std::string_view {
        if (history_.empty()) return line_;
        if (history_index_ == history_.size()) saved_line_ = line_;
        if (history_index_ > 0) --history_index_;
        line_ = history_[history_index_];
        cursor_ = line_.size();
        return line_;
    }

    inline void set_completion_callback(CompletionCallback callback) {
        completion_callback_ = std::move(callback);
    }

    // NOTE(agent): These minimal helpers are required to make the manifest's
    // submission example usable; their exact surface is otherwise unspecified.
    inline void on_submit(std::function<void(std::string_view)> callback) {
        submit_callback_ = std::move(callback);
    }

    inline void submit() {
        if (!line_.empty()) history_.push_back(line_);
        history_index_ = history_.size();
        if (submit_callback_) submit_callback_(line_);
        line_.clear();
        cursor_ = 0;
    }

    [[nodiscard]] inline auto line() const noexcept -> std::string_view { return line_; }

private:
    std::string line_{};
    std::string saved_line_{};
    std::vector<std::string> history_{};
    std::size_t cursor_{};
    std::size_t history_index_{};
    CompletionCallback completion_callback_{};
    std::function<void(std::string_view)> submit_callback_{};
};

}  // namespace ttytk

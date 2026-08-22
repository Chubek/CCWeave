#pragma once

#include <TTyUtils/TTyUtils-Search.hpp>
#include <TTyUtils/TTyUtils-SyntaxHighlight.hpp>
#include <TTyUtils/TTyUtils-TextCursor.hpp>
#include <TTyUtils/TTyUtils-UndoHistory.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>
#include <TTyUtils/TTyUtils-WordWrap.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace ttyutils::ui {

struct EditorOptions {
    bool wrap{};
    bool center_on_jump{};
    std::size_t scrolloff{2};
};

class Gutter {
public:
    void show_line_numbers(const bool value) noexcept { line_numbers_ = value; }
    [[nodiscard]] auto line_numbers() const noexcept -> bool { return line_numbers_; }

private:
    bool line_numbers_{true};
};

class TextArea : public Widget {
public:
    TextArea() : history_(buffer_) {}

    [[nodiscard]] auto open(const std::string& path) -> bool {
        auto loaded = textbuf::load_file(path);
        if (!loaded) return false;
        buffer_ = std::move(loaded).value();
        history_ = undo::History{buffer_};
        return true;
    }
    void set_text(std::string text) {
        const auto lines = buffer_.line_index().lines();
        buffer_.erase({0, 0}, {lines - 1, ttytk::grapheme::count(buffer_.line(lines - 1))});
        buffer_.insert({0, 0}, text);
    }
    [[nodiscard]] auto text() const noexcept -> std::string_view { return buffer_.text(); }
    [[nodiscard]] auto options() noexcept -> EditorOptions& { return options_; }
    [[nodiscard]] auto gutter() noexcept -> Gutter& { return gutter_; }
    [[nodiscard]] auto cursor() noexcept -> textcur::CursorSet& { return cursors_; }

    void paint(Canvas& canvas) override {
        std::size_t line = 0;
        for (const auto& value : buffer_.text()) {
            if (value == '\n') ++line;
        }
        (void)line;
        canvas.text(rect().x, rect().y, buffer_.line(0), style());
    }

private:
    textbuf::Buffer buffer_;
    textcur::CursorSet cursors_{buffer_};
    undo::History history_;
    EditorOptions options_{};
    Gutter gutter_{};
};

}  // namespace ttyutils::ui

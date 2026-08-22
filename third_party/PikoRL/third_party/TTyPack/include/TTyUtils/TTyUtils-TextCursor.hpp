#pragma once

#include <TTyTk/TTyTk-Grampheme.hpp>
#include <TTyUtils/TTyUtils-TextBuffer.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::textcur {

using Position = textbuf::Buffer::Position;

enum class Motion { left, right, up, down, word_left, word_right, paragraph_up, paragraph_down };

struct Selection {
    Position anchor{};
    Position active{};

    [[nodiscard]] auto begin() const noexcept -> Position { return std::min(anchor, active, less); }
    [[nodiscard]] auto end() const noexcept -> Position { return std::max(anchor, active, less); }

private:
    static constexpr auto less(const Position& left, const Position& right) noexcept -> bool {
        return left.line < right.line || (left.line == right.line && left.col < right.col);
    }
};

// NOTE(agent): TextCursor's manifest requires Unicode-aware word motion but
// exposes no Unicode classification dependency. Non-ASCII graphemes are kept
// together and treated as word characters; ASCII uses the conventional editor rule.
[[nodiscard]] inline auto word_boundary(const std::string_view text, const std::size_t byte) -> bool {
    if (byte == 0 || byte >= text.size()) return true;
    const auto classify = [](const unsigned char character) {
        return std::isalnum(character) != 0 || character == '_';
    };
    return classify(static_cast<unsigned char>(text[byte - 1])) !=
           classify(static_cast<unsigned char>(text[byte]));
}

class Cursor {
public:
    Cursor() = default;
    explicit Cursor(const Position position) : position_(position), sticky_column_(position.col) {}

    [[nodiscard]] auto position() const noexcept -> Position { return position_; }
    [[nodiscard]] auto selection() const noexcept -> Selection { return {anchor_, position_}; }
    void set_position(const Position position, const bool keep_anchor = false) noexcept {
        position_ = position;
        if (!keep_anchor) anchor_ = position;
        sticky_column_ = position.col;
    }

    void move(const textbuf::Buffer& buffer, const Motion motion, const bool extend = false) {
        if (!extend) anchor_ = position_;
        const auto lines = buffer.line_index().lines();
        const auto line_length = [&buffer](const std::size_t line) {
            return ttytk::grapheme::count(buffer.line(line));
        };
        switch (motion) {
        case Motion::left:
            if (position_.col > 0) --position_.col;
            else if (position_.line > 0) { --position_.line; position_.col = line_length(position_.line); }
            break;
        case Motion::right:
            if (position_.col < line_length(position_.line)) ++position_.col;
            else if (position_.line + 1 < lines) { ++position_.line; position_.col = 0; }
            break;
        case Motion::up:
            if (position_.line > 0) { --position_.line; position_.col = std::min(sticky_column_, line_length(position_.line)); }
            break;
        case Motion::down:
            if (position_.line + 1 < lines) { ++position_.line; position_.col = std::min(sticky_column_, line_length(position_.line)); }
            break;
        case Motion::word_left: move_word(buffer, false); break;
        case Motion::word_right: move_word(buffer, true); break;
        case Motion::paragraph_up:
            while (position_.line > 0 && buffer.line(position_.line).empty()) --position_.line;
            while (position_.line > 0 && !buffer.line(position_.line - 1).empty()) --position_.line;
            position_.col = 0;
            break;
        case Motion::paragraph_down:
            while (position_.line + 1 < lines && !buffer.line(position_.line + 1).empty()) ++position_.line;
            while (position_.line + 1 < lines && buffer.line(position_.line).empty()) ++position_.line;
            position_.col = 0;
            break;
        }
        sticky_column_ = position_.col;
    }

private:
    void move_word(const textbuf::Buffer& buffer, const bool forward) {
        auto index = buffer.line_index();
        auto offset = index.offset(position_, buffer.text());
        const auto is_word = [](const char value) {
            return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_';
        };
        if (forward) {
            const bool started_word = offset < buffer.text().size() && is_word(buffer.text()[offset]);
            while (offset < buffer.text().size() && is_word(buffer.text()[offset]))
                offset = ttytk::grapheme::next_break(buffer.text(), offset);
            if (!started_word) {
                while (offset < buffer.text().size() && !is_word(buffer.text()[offset]))
                    offset = ttytk::grapheme::next_break(buffer.text(), offset);
            }
        } else {
            while (offset > 0) {
                const auto prior = [&buffer, offset] {
                    std::size_t result = 0;
                    while (ttytk::grapheme::next_break(buffer.text(), result) < offset)
                        result = ttytk::grapheme::next_break(buffer.text(), result);
                    return result;
                }();
                offset = prior;
                if (is_word(buffer.text()[offset])) break;
            }
            while (offset > 0) {
                const auto prior = [&buffer, offset] {
                    std::size_t result = 0;
                    while (ttytk::grapheme::next_break(buffer.text(), result) < offset)
                        result = ttytk::grapheme::next_break(buffer.text(), result);
                    return result;
                }();
                if (!is_word(buffer.text()[prior])) break;
                offset = prior;
            }
        }
        position_ = index.position(offset, buffer.text());
    }

    Position position_{};
    Position anchor_{};
    std::size_t sticky_column_{};
};

class CursorSet {
public:
    explicit CursorSet(textbuf::Buffer& buffer) : buffer_(&buffer) {}

    void add(const Position position) { cursors_.emplace_back(position); merge(); }
    [[nodiscard]] auto cursors() const noexcept -> const std::vector<Cursor>& { return cursors_; }

    void move(const Motion motion, const bool extend = false) {
        for (auto& cursor : cursors_) cursor.move(*buffer_, motion, extend);
        merge();
    }

    void insert_each(const std::string_view text) {
        std::sort(cursors_.begin(), cursors_.end(), [](const Cursor& left, const Cursor& right) {
            const auto a = left.position(); const auto b = right.position();
            return a.line > b.line || (a.line == b.line && a.col > b.col);
        });
        for (auto& cursor : cursors_) {
            const auto position = cursor.position();
            buffer_->insert(position, text);
            cursor.set_position({position.line, position.col + ttytk::grapheme::count(text)});
        }
        merge();
    }

private:
    void merge() {
        std::sort(cursors_.begin(), cursors_.end(), [](const Cursor& left, const Cursor& right) {
            const auto a = left.position(); const auto b = right.position();
            return a.line < b.line || (a.line == b.line && a.col < b.col);
        });
        cursors_.erase(std::unique(cursors_.begin(), cursors_.end(), [](const Cursor& left, const Cursor& right) {
            return left.position() == right.position();
        }), cursors_.end());
    }

    textbuf::Buffer* buffer_{};
    std::vector<Cursor> cursors_;
};

}  // namespace ttyutils::textcur

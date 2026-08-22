#pragma once

#include <TTyTk/TTyTk-Grampheme.hpp>
#include <TTyUtils/TTyUtils-TextBuffer.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::wrap {

class BreakRules {
public:
    constexpr BreakRules(bool break_words = true, bool preserve_code_tokens = false) noexcept
        : break_words_(break_words), preserve_code_tokens_(preserve_code_tokens) {}

    [[nodiscard]] static constexpr auto prose() noexcept -> BreakRules { return {}; }
    [[nodiscard]] static constexpr auto code() noexcept -> BreakRules { return {true, true}; }
    [[nodiscard]] constexpr auto break_words() const noexcept -> bool { return break_words_; }
    [[nodiscard]] constexpr auto preserve_code_tokens() const noexcept -> bool { return preserve_code_tokens_; }

private:
    bool break_words_{};
    bool preserve_code_tokens_{};
};

class WrapMap {
public:
    struct VisualPosition {
        std::size_t row{};
        std::size_t col{};
    };

    struct Row {
        textbuf::Buffer::Position start{};
        textbuf::Buffer::Position end{};
    };

    [[nodiscard]] auto rows() const noexcept -> const std::vector<Row>& { return rows_; }

    [[nodiscard]] auto visual_to_buffer(const VisualPosition position) const -> textbuf::Buffer::Position {
        if (rows_.empty()) return {};
        const auto& row = rows_[position.row < rows_.size() ? position.row : rows_.size() - 1];
        return {row.start.line, row.start.col + (position.col < row.end.col - row.start.col
                                                     ? position.col : row.end.col - row.start.col)};
    }

    [[nodiscard]] auto buffer_to_visual(const textbuf::Buffer::Position position) const -> VisualPosition {
        for (std::size_t row = 0; row < rows_.size(); ++row) {
            const auto& current = rows_[row];
            if (current.start.line == position.line && current.end.line == position.line &&
                position.col >= current.start.col && position.col <= current.end.col)
                return {row, position.col - current.start.col};
        }
        return {};
    }

private:
    friend auto soft_wrap(const textbuf::Snapshot&, std::size_t, BreakRules) -> WrapMap;
    std::vector<Row> rows_;
};

[[nodiscard]] inline auto soft_wrap(const textbuf::Snapshot& snapshot, const std::size_t width,
                                    const BreakRules rules = BreakRules::prose()) -> WrapMap {
    WrapMap map;
    const auto index = snapshot.line_index();
    const auto limit = width == 0 ? 1 : width;
    for (std::size_t line = 0; line < index.lines(); ++line) {
        const auto text = [&snapshot, &index, line] {
            const auto start = index.offset({line, 0}, snapshot.text());
            const auto next = line + 1 < index.lines() ? index.offset({line + 1, 0}, snapshot.text()) : snapshot.text().size();
            const auto end = next > start && snapshot.text()[next - 1] == '\n' ? next - 1 : next;
            return snapshot.text().substr(start, end - start);
        }();
        if (text.empty()) {
            map.rows_.push_back({{line, 0}, {line, 0}});
            continue;
        }
        std::size_t start = 0;
        while (start < text.size()) {
            std::size_t end = start;
            std::size_t columns = 0;
            std::size_t last_space = start;
            while (end < text.size() && columns < limit) {
                const auto next = ttytk::grapheme::next_break(text, end);
                if (text[end] == ' ' || text[end] == '\t') last_space = next;
                end = next;
                ++columns;
            }
            // UAX #14: spaces are ordinary indirect break opportunities.
            if (end < text.size() && rules.break_words() && last_space > start) end = last_space;
            const auto start_col = ttytk::grapheme::count(text.substr(0, start));
            const auto end_col = ttytk::grapheme::count(text.substr(0, end));
            map.rows_.push_back({{line, start_col}, {line, end_col}});
            start = end;
            while (start < text.size() && text[start] == ' ') ++start;
        }
    }
    return map;
}

// NOTE(agent): The manifest's reflow example references Selection::range(),
// but neither a range symbol nor TextCursor is a dependency. This conservative
// overload reflows the complete buffer.
inline void reflow(textbuf::Buffer& buffer, const std::size_t width,
                   const BreakRules rules = BreakRules::prose()) {
    const auto map = soft_wrap(buffer.snapshot(), width, rules);
    std::string result;
    for (const auto& row : map.rows()) {
        const auto source = buffer.line(row.start.line);
        std::size_t start = 0;
        for (std::size_t i = 0; i < row.start.col; ++i) start = ttytk::grapheme::next_break(source, start);
        std::size_t end = start;
        for (std::size_t i = row.start.col; i < row.end.col; ++i) end = ttytk::grapheme::next_break(source, end);
        result.append(source.substr(start, end - start));
        result.push_back('\n');
    }
    const auto lines = buffer.line_index().lines();
    buffer.erase({0, 0}, {lines - 1, ttytk::grapheme::count(buffer.line(lines - 1))});
    buffer.insert({0, 0}, result);
}

}  // namespace ttyutils::wrap

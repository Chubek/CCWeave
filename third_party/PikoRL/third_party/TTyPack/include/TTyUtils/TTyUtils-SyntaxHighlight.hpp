#pragma once

#include <TTyUtils/TTyUtils-SyntaxGrammar.hpp>
#include <TTyUtils/TTyUtils-TextBuffer.hpp>
#include <TTyUtils/TTyUtils-Theme.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::hl {

class SpanList {
public:
    struct Item {
        std::size_t begin{};
        std::size_t end{};
        syntax::Scope scope{};
    };

    [[nodiscard]] auto items() const noexcept -> const std::vector<Item>& { return items_; }
    void add(Item item) { items_.push_back(std::move(item)); }

private:
    std::vector<Item> items_;
};

struct LineState {
    std::size_t generation{};
    bool lexical_only{};
};

class Incremental {
public:
    void invalidate_from(const std::size_t line) noexcept { first_dirty_ = std::min(first_dirty_, line); }
    [[nodiscard]] auto first_dirty() const noexcept -> std::size_t { return first_dirty_; }
    void clean() noexcept { first_dirty_ = static_cast<std::size_t>(-1); }

private:
    std::size_t first_dirty_{static_cast<std::size_t>(-1)};
};

// NOTE(agent): Theme's concrete Style type belongs to the empty Phase 7
// header at this phase. to_style therefore returns the scope's stable role
// name, which Phase 7's theme layer can resolve without a circular dependency.
[[nodiscard]] inline auto to_style(const syntax::Scope& scope) -> std::string {
    return std::string(scope.name());
}

class Highlighter {
public:
    explicit Highlighter(const syntax::Grammar& grammar) : grammar_(&grammar) {}

    template <typename Theme>
    Highlighter(const syntax::Grammar& grammar, const Theme&) : grammar_(&grammar) {}

    void attach(textbuf::Buffer& buffer) {
        buffer_ = &buffer;
        observed_generation_ = 0;
        incremental_.invalidate_from(0);
    }

    [[nodiscard]] auto line_spans(const std::size_t line) -> SpanList {
        if (buffer_ == nullptr || line >= buffer_->line_index().lines()) return {};
        if (observed_generation_ != buffer_->generation()) {
            incremental_.invalidate_from(0);
            observed_generation_ = buffer_->generation();
        }
        const auto source = buffer_->line(line);
        SpanList result;
        if (source.size() > line_limit_) {
            states_.resize(std::max(states_.size(), line + 1));
            states_[line] = {observed_generation_, true};
            return result;
        }
        for (const auto& keyword : grammar_->keywords) {
            for (std::size_t at = source.find(keyword); at != std::string_view::npos;
                 at = source.find(keyword, at + keyword.size())) {
                const auto left_ok = at == 0 || !is_word(source[at - 1]);
                const auto end = at + keyword.size();
                const auto right_ok = end == source.size() || !is_word(source[end]);
                if (left_ok && right_ok) result.add({at, end, grammar_->root_scope});
            }
        }
        states_.resize(std::max(states_.size(), line + 1));
        states_[line] = {observed_generation_, false};
        incremental_.clean();
        if (ready_) ready_(line, line);
        return result;
    }

    void on_ready(std::function<void(int, int)> callback) { ready_ = std::move(callback); }
    void set_line_limit(const std::size_t limit) noexcept { line_limit_ = limit; }

private:
    [[nodiscard]] static auto is_word(const char value) noexcept -> bool {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
               (value >= '0' && value <= '9') || value == '_';
    }

    const syntax::Grammar* grammar_{};
    textbuf::Buffer* buffer_{};
    std::vector<LineState> states_;
    Incremental incremental_;
    std::function<void(int, int)> ready_{};
    std::size_t observed_generation_{};
    std::size_t line_limit_{4096};
};

}  // namespace ttyutils::hl

#pragma once

#include <TTyTk/TTyTk-Grampheme.hpp>
#include <TTyUtils/TTyUtils-TextBuffer.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::search {

struct Match {
    textbuf::Buffer::Position begin{};
    textbuf::Buffer::Position end{};
    int score{};
};

class Query {
public:
    enum class Kind { literal, regex, fuzzy };

    [[nodiscard]] static auto literal(std::string value, const bool case_sensitive = true) -> Query {
        return {Kind::literal, std::move(value), case_sensitive};
    }
    [[nodiscard]] static auto regex(std::string value, const bool case_sensitive = true) -> Query {
        return {Kind::regex, std::move(value), case_sensitive};
    }
    [[nodiscard]] static auto fuzzy(std::string value, const bool case_sensitive = false) -> Query {
        return {Kind::fuzzy, std::move(value), case_sensitive};
    }

    [[nodiscard]] auto kind() const noexcept -> Kind { return kind_; }
    [[nodiscard]] auto text() const noexcept -> std::string_view { return text_; }
    [[nodiscard]] auto case_sensitive() const noexcept -> bool { return case_sensitive_; }

private:
    Query(Kind kind, std::string value, bool case_sensitive)
        : kind_(kind), text_(std::move(value)), case_sensitive_(case_sensitive) {}
    Kind kind_;
    std::string text_;
    bool case_sensitive_{};
};

namespace detail {

[[nodiscard]] inline auto equal(const char left, const char right, const bool case_sensitive) -> bool {
    if (case_sensitive) return left == right;
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
}

[[nodiscard]] inline auto literal_at(const std::string_view text, const std::size_t offset,
                                     const Query& query) -> std::optional<std::size_t> {
    if (offset + query.text().size() > text.size()) return std::nullopt;
    for (std::size_t index = 0; index < query.text().size(); ++index) {
        if (!equal(text[offset + index], query.text()[index], query.case_sensitive())) return std::nullopt;
    }
    return offset + query.text().size();
}

// A compact backtracking NFA for '.', '*', '^', '$', and escaped literals.
[[nodiscard]] inline auto regex_at(const std::string_view pattern, const std::string_view text,
                                   const std::size_t pattern_at, const std::size_t text_at,
                                   const bool case_sensitive) -> std::optional<std::size_t> {
    if (pattern_at == pattern.size()) return text_at;
    if (pattern[pattern_at] == '$' && pattern_at + 1 == pattern.size())
        return text_at == text.size() ? std::optional{text_at} : std::nullopt;
    bool escaped = pattern[pattern_at] == '\\' && pattern_at + 1 < pattern.size();
    const auto atom_at = pattern_at + (escaped ? 1U : 0U);
    const auto next = atom_at + 1;
    const bool star = next < pattern.size() && pattern[next] == '*';
    const auto atom_matches = [&](const std::size_t offset) {
        return offset < text.size() &&
               (pattern[atom_at] == '.' || equal(pattern[atom_at], text[offset], case_sensitive));
    };
    if (star) {
        auto cursor = text_at;
        do {
            if (const auto rest = regex_at(pattern, text, next + 1, cursor, case_sensitive)) return rest;
            if (!atom_matches(cursor)) break;
            ++cursor;
        } while (cursor <= text.size());
        return std::nullopt;
    }
    if (!atom_matches(text_at)) return std::nullopt;
    return regex_at(pattern, text, next, text_at + 1, case_sensitive);
}

[[nodiscard]] inline auto fuzzy_at(const std::string_view text, const std::size_t offset,
                                   const Query& query) -> std::optional<std::pair<std::size_t, int>> {
    std::size_t cursor = offset;
    int score = 0;
    for (const auto needle : query.text()) {
        while (cursor < text.size() && !equal(needle, text[cursor], query.case_sensitive())) {
            ++cursor;
            --score;
        }
        if (cursor == text.size()) return std::nullopt;
        score += cursor == offset || !std::isalnum(static_cast<unsigned char>(text[cursor - 1])) ? 10 : 1;
        ++cursor;
    }
    return std::pair{cursor, score};
}

}  // namespace detail

// NOTE(agent): Full Unicode case folding and capture references require data
// tables absent from the allowed dependencies. This no-dependency engine uses
// ASCII folding and supports the documented regex mode without std::regex.
[[nodiscard]] inline auto find_all(const textbuf::Snapshot& snapshot, const Query& query) -> std::vector<Match> {
    std::vector<Match> matches;
    const auto text = snapshot.text();
    const auto index = snapshot.line_index();
    if (query.text().empty()) return matches;
    const auto anchored = query.kind() == Query::Kind::regex && query.text().starts_with('^');
    const auto pattern = anchored ? query.text().substr(1) : query.text();
    for (std::size_t offset = 0; offset < text.size();) {
        std::optional<std::size_t> end;
        int score = 0;
        if (query.kind() == Query::Kind::literal) end = detail::literal_at(text, offset, query);
        else if (query.kind() == Query::Kind::regex) end = detail::regex_at(pattern, text, 0, offset, query.case_sensitive());
        else if (const auto fuzzy = detail::fuzzy_at(text, offset, query)) { end = fuzzy->first; score = fuzzy->second; }
        if (end) {
            matches.push_back({index.position(offset, text), index.position(*end, text), score});
            offset = *end > offset ? *end : ttytk::grapheme::next_break(text, offset);
            if (anchored) break;
        } else {
            if (anchored) break;
            offset = ttytk::grapheme::next_break(text, offset);
        }
    }
    return matches;
}

[[nodiscard]] inline auto find(const textbuf::Snapshot& snapshot, const Query& query) -> std::optional<Match> {
    const auto matches = find_all(snapshot, query);
    return matches.empty() ? std::nullopt : std::optional<Match>{matches.front()};
}

[[nodiscard]] inline auto incremental(const textbuf::Snapshot& snapshot, const Query& query,
                                      const std::vector<Match>& previous = {}) -> std::vector<Match> {
    (void)previous;
    return find_all(snapshot, query);
}

class Replacer {
public:
    Replacer(textbuf::Buffer& buffer, Query query, std::string replacement)
        : buffer_(&buffer), query_(std::move(query)), replacement_(std::move(replacement)) {}

    template <typename Confirm>
    auto confirm_each(Confirm&& confirm) -> std::size_t {
        auto matches = find_all(buffer_->snapshot(), query_);
        std::size_t applied = 0;
        for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
            if (!std::invoke(std::forward<Confirm>(confirm), *it)) continue;
            buffer_->replace(it->begin, it->end, replacement_);
            ++applied;
        }
        return applied;
    }

    auto replace_all() -> std::size_t {
        return confirm_each([](const Match&) { return true; });
    }

private:
    textbuf::Buffer* buffer_{};
    Query query_;
    std::string replacement_;
};

}  // namespace ttyutils::search

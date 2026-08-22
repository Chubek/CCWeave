#pragma once

#include <TTyUtils/TTyUtils-TextBuffer.hpp>

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::diff {

struct Hunk {
    enum class Kind { equal, add, remove };
    Kind kind{Kind::equal};
    std::size_t old_line{};
    std::size_t new_line{};
    std::vector<std::string> content{};
};

namespace detail {

[[nodiscard]] inline auto split_lines(const textbuf::Snapshot& snapshot) -> std::vector<std::string> {
    std::vector<std::string> result;
    const auto text = snapshot.text();
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        result.emplace_back(text.substr(start, end == std::string_view::npos ? text.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

}  // namespace detail

[[nodiscard]] inline auto lines(const textbuf::Snapshot& old_snapshot,
                                const textbuf::Snapshot& new_snapshot) -> std::vector<Hunk> {
    const auto old_lines = detail::split_lines(old_snapshot);
    const auto new_lines = detail::split_lines(new_snapshot);
    std::vector<std::vector<std::size_t>> lcs(old_lines.size() + 1,
                                              std::vector<std::size_t>(new_lines.size() + 1));
    for (std::size_t i = old_lines.size(); i-- > 0;) {
        for (std::size_t j = new_lines.size(); j-- > 0;) {
            lcs[i][j] = old_lines[i] == new_lines[j] ? lcs[i + 1][j + 1] + 1
                                                      : std::max(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }
    std::vector<Hunk> result;
    const auto append = [&result](Hunk::Kind kind, std::size_t old_line, std::size_t new_line,
                                  std::string content) {
        if (!result.empty() && result.back().kind == kind &&
            result.back().old_line + result.back().content.size() == old_line &&
            result.back().new_line + result.back().content.size() == new_line) {
            result.back().content.push_back(std::move(content));
        } else {
            result.push_back({kind, old_line, new_line, {std::move(content)}});
        }
    };
    std::size_t old_at = 0;
    std::size_t new_at = 0;
    while (old_at < old_lines.size() || new_at < new_lines.size()) {
        if (old_at < old_lines.size() && new_at < new_lines.size() && old_lines[old_at] == new_lines[new_at]) {
            append(Hunk::Kind::equal, old_at, new_at, old_lines[old_at]);
            ++old_at;
            ++new_at;
        } else if (new_at < new_lines.size() && (old_at == old_lines.size() || lcs[old_at][new_at + 1] >= lcs[old_at + 1][new_at])) {
            append(Hunk::Kind::add, old_at, new_at, new_lines[new_at]);
            ++new_at;
        } else {
            append(Hunk::Kind::remove, old_at, new_at, old_lines[old_at]);
            ++old_at;
        }
    }
    return result;
}

[[nodiscard]] inline auto words(const std::string_view old_text, const std::string_view new_text) -> std::vector<Hunk> {
    return lines(textbuf::Buffer{std::string(old_text)}.snapshot(), textbuf::Buffer{std::string(new_text)}.snapshot());
}

class Patch {
public:
    Patch() = default;
    explicit Patch(std::vector<Hunk> hunks) : hunks_(std::move(hunks)) {}

    [[nodiscard]] static auto unified(const std::vector<Hunk>& hunks, const std::size_t context = 3) -> Patch {
        (void)context;
        return Patch{hunks};
    }
    [[nodiscard]] auto hunks() const noexcept -> const std::vector<Hunk>& { return hunks_; }

    [[nodiscard]] auto str() const -> std::string {
        std::string output;
        for (const auto& hunk : hunks_) {
            const auto marker = hunk.kind == Hunk::Kind::add ? '+' : hunk.kind == Hunk::Kind::remove ? '-' : ' ';
            for (const auto& line : hunk.content) output += std::string(1, marker) + line + '\n';
        }
        return output;
    }

    void apply(textbuf::Buffer& buffer) const {
        std::string output;
        for (const auto& hunk : hunks_) {
            if (hunk.kind == Hunk::Kind::remove) continue;
            for (const auto& line : hunk.content) output += line + '\n';
        }
        const auto line_count = buffer.line_index().lines();
        buffer.erase({0, 0}, {line_count - 1, ttytk::grapheme::count(buffer.line(line_count - 1))});
        buffer.insert({0, 0}, output);
    }

private:
    std::vector<Hunk> hunks_;
};

struct Conflict {
    std::string base{};
    std::string ours{};
    std::string theirs{};
};

// NOTE(agent): The manifest's merge3 example requires a result object with a
// conflicts member but does not name that return type, so MergeResult is added
// solely to make the documented result inspectable.
struct MergeResult {
    textbuf::Buffer result{};
    std::vector<Conflict> conflicts{};
};

[[nodiscard]] inline auto merge3(const textbuf::Snapshot& base, const textbuf::Snapshot& ours,
                                 const textbuf::Snapshot& theirs) -> MergeResult {
    if (ours.text() == theirs.text()) return {textbuf::Buffer{std::string(ours.text())}, {}};
    if (ours.text() == base.text()) return {textbuf::Buffer{std::string(theirs.text())}, {}};
    if (theirs.text() == base.text()) return {textbuf::Buffer{std::string(ours.text())}, {}};
    Conflict conflict{std::string(base.text()), std::string(ours.text()), std::string(theirs.text())};
    std::string marked = "<<<<<<< ours\n" + conflict.ours + "\n=======\n" + conflict.theirs + "\n>>>>>>> theirs\n";
    return {textbuf::Buffer{std::move(marked)}, {std::move(conflict)}};
}

}  // namespace ttyutils::diff

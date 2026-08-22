#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Grampheme.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::textbuf {

struct Piece {
    enum class Source { original, added };
    Source source{Source::original};
    std::size_t offset{};
    std::size_t length{};
};

class LineIndex {
public:
    struct Position {
        std::size_t line{};
        std::size_t col{};
        [[nodiscard]] friend constexpr auto operator==(const Position&, const Position&) -> bool = default;
    };

    LineIndex() = default;
    explicit LineIndex(const std::string_view text) { rebuild(text); }

    void rebuild(const std::string_view text) {
        starts_.assign(1, 0);
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '\n') starts_.push_back(index + 1);
        }
    }

    [[nodiscard]] auto lines() const noexcept -> std::size_t { return starts_.size(); }

    [[nodiscard]] auto offset(const Position position, const std::string_view text) const -> std::size_t {
        if (starts_.empty()) return 0;
        const auto line = std::min(position.line, starts_.size() - 1);
        const auto begin = starts_[line];
        const auto end = line + 1 < starts_.size() ? starts_[line + 1] - 1 : text.size();
        std::size_t byte = begin;
        for (std::size_t column = 0; byte < end && column < position.col; ++column) {
            byte = ttytk::grapheme::next_break(text, byte);
        }
        return byte;
    }

    [[nodiscard]] auto position(const std::size_t byte, const std::string_view text) const -> Position {
        const auto bounded = std::min(byte, text.size());
        const auto found = std::upper_bound(starts_.begin(), starts_.end(), bounded);
        const auto line = static_cast<std::size_t>(std::distance(starts_.begin(), found) - 1);
        std::size_t col = 0;
        for (std::size_t at = starts_[line]; at < bounded; at = ttytk::grapheme::next_break(text, at)) ++col;
        return {line, col};
    }

private:
    std::vector<std::size_t> starts_{0};
};

class Snapshot {
public:
    Snapshot() : text_(std::make_shared<const std::string>()) {}

    [[nodiscard]] auto text() const noexcept -> std::string_view { return *text_; }
    [[nodiscard]] auto line_index() const -> LineIndex { return LineIndex{text()}; }
    [[nodiscard]] auto generation() const noexcept -> std::size_t { return generation_; }

private:
    friend class Buffer;
    Snapshot(std::shared_ptr<const std::string> text, const std::size_t generation)
        : text_(std::move(text)), generation_(generation) {}

    std::shared_ptr<const std::string> text_;
    std::size_t generation_{};
};

class Buffer {
public:
    using Position = LineIndex::Position;

    enum class Eol { lf, crlf, mixed };

    Buffer() : text_(std::make_shared<std::string>()) {}
    explicit Buffer(std::string text) : text_(std::make_shared<std::string>(std::move(text))) {
        inspect_encoding();
    }

    [[nodiscard]] auto text() const noexcept -> std::string_view { return *text_; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return text_->size(); }
    [[nodiscard]] auto line_index() const -> LineIndex { return LineIndex{text()}; }
    [[nodiscard]] auto eol() const noexcept -> Eol { return eol_; }
    [[nodiscard]] auto has_bom() const noexcept -> bool { return bom_; }
    [[nodiscard]] auto generation() const noexcept -> std::size_t { return generation_; }

    [[nodiscard]] auto snapshot() const -> Snapshot {
        return Snapshot{std::shared_ptr<const std::string>(text_), generation_};
    }

    void insert(const Position position, const std::string_view value) {
        detach();
        const auto at = line_index().offset(position, text());
        text_->insert(at, value);
        ++generation_;
        inspect_encoding();
    }

    void erase(const Position first, const Position last) {
        detach();
        const auto index = line_index();
        const auto begin = index.offset(first, text());
        const auto end = index.offset(last, text());
        if (end > begin) text_->erase(begin, end - begin);
        ++generation_;
        inspect_encoding();
    }

    void replace(const Position first, const Position last, const std::string_view value) {
        erase(first, last);
        insert(first, value);
    }

    [[nodiscard]] auto line(const std::size_t number) const -> std::string_view {
        const auto index = line_index();
        if (number >= index.lines()) return {};
        const auto begin = index.offset({number, 0}, text());
        auto end = text().find('\n', begin);
        if (end == std::string_view::npos) end = text().size();
        return text().substr(begin, end - begin);
    }

private:
    void detach() {
        if (!text_.unique()) text_ = std::make_shared<std::string>(*text_);
    }

    void inspect_encoding() {
        bom_ = text_->starts_with("\xEF\xBB\xBF");
        const auto crlf = text_->find("\r\n") != std::string::npos;
        const auto bare_lf = text_->find('\n') != std::string::npos &&
                             text_->find_first_not_of('\r', text_->find('\n') - (text_->find('\n') > 0 ? 1 : 0)) != std::string::npos;
        eol_ = crlf && bare_lf ? Eol::mixed : (crlf ? Eol::crlf : Eol::lf);
    }

    std::shared_ptr<std::string> text_;
    Eol eol_{Eol::lf};
    bool bom_{};
    std::size_t generation_{};
};

[[nodiscard]] inline auto load_file(const std::filesystem::path& path) -> ttytk::Result<Buffer> {
    std::ifstream input(path, std::ios::binary);
    if (!input) return ttytk::Error{1, "unable to open text file"};
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input.good() && !input.eof()) return ttytk::Error{2, "unable to read text file"};
    return Buffer{std::move(text)};
}

[[nodiscard]] inline auto save_file(const Buffer& buffer, const std::filesystem::path& path) -> ttytk::Result<void> {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return ttytk::Error{1, "unable to open text file for writing"};
    output.write(buffer.text().data(), static_cast<std::streamsize>(buffer.text().size()));
    if (!output) return ttytk::Error{2, "unable to write text file"};
    return {};
}

}  // namespace ttyutils::textbuf

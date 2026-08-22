#pragma once

#include <TTyUtils/TTyUtils-ScrollBar.hpp>
#include <TTyUtils/TTyUtils-Search.hpp>
#include <TTyUtils/TTyUtils-StatusBar.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>
#include <TTyUtils/TTyUtils-WordWrap.hpp>

#include <filesystem>
#include <fstream>
#include <istream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::pager {

class Source {
public:
    Source() = default;
    explicit Source(std::string text) : text_(std::move(text)) {}
    [[nodiscard]] static auto stream(std::string text) -> Source { return Source{std::move(text)}; }
    [[nodiscard]] auto text() const noexcept -> std::string_view { return text_; }

private:
    std::string text_;
};

struct Marks {
    std::vector<std::size_t> rows{};
};

class Pager : public ui::Widget, public ui::Scrollable {
public:
    explicit Pager(Source source = {}) : source_(std::move(source)) {}
    void set_source(Source source) { source_ = std::move(source); offset_ = 0; }
    void follow(const bool value) noexcept { follow_ = value; }
    void set_marks(Marks marks) { marks_ = std::move(marks); }
    [[nodiscard]] auto offset() const -> std::size_t override { return offset_; }
    void scroll_to(const std::size_t offset) override { offset_ = std::min(offset, length()); }
    [[nodiscard]] auto length() const -> std::size_t override { return lines().size(); }
    [[nodiscard]] auto lines() const -> std::vector<std::string_view> {
        std::vector<std::string_view> result;
        std::size_t start = 0;
        while (start <= source_.text().size()) {
            const auto end = source_.text().find('\n', start);
            result.push_back(source_.text().substr(start, end == std::string_view::npos ? source_.text().size() - start : end - start));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return result;
    }
    void paint(ui::Canvas& canvas) override {
        const auto values = lines();
        for (std::size_t row = offset_; row < values.size() && row - offset_ < static_cast<std::size_t>(std::max(0, rect().height)); ++row)
            canvas.text(rect().x, rect().y + static_cast<int>(row - offset_), values[row], style());
    }

private:
    Source source_;
    Marks marks_;
    std::size_t offset_{};
    bool follow_{};
};

inline auto page_file(const std::filesystem::path& path) -> Pager {
    std::ifstream input(path);
    return Pager{Source{std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}}};
}

inline auto page_stream(std::istream& input) -> Pager {
    return Pager{Source{std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}}};
}

}  // namespace ttyutils::pager

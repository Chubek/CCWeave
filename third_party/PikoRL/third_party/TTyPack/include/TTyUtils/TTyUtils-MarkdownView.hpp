#pragma once

#include <TTyUtils/TTyUtils-Pager.hpp>
#include <TTyUtils/TTyUtils-SyntaxHighlight.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>
#include <TTyUtils/TTyUtils-WordWrap.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ttyutils::mdview {

struct Options {
    bool links{true};
    bool code_highlighting{true};
    std::size_t width{80};
};

struct TocEntry {
    std::string title{};
    std::size_t line{};
    int level{};
};

// NOTE(agent): TTyTk::Hyperlink is not present in this repository. Markdown
// links remain text labels; OSC 8 emission can be added when that dependency
// exists.
inline auto render(const std::string_view markdown, const Options& options = {}) -> std::string {
    (void)options;
    std::string output;
    std::size_t start = 0;
    while (start <= markdown.size()) {
        const auto end = markdown.find('\n', start);
        auto line = markdown.substr(start, end == std::string_view::npos ? markdown.size() - start : end - start);
        while (line.starts_with('#')) line.remove_prefix(1);
        if (line.starts_with(' ')) line.remove_prefix(1);
        output += std::string(line);
        output.push_back('\n');
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return output;
}

class MarkdownView : public ui::Widget {
public:
    void set_text(std::string text) {
        text_ = std::move(text);
        toc_.clear();
        std::size_t line = 0;
        std::size_t start = 0;
        while (start <= text_.size()) {
            const auto end = text_.find('\n', start);
            const auto value = std::string_view(text_).substr(start, end == std::string::npos ? text_.size() - start : end - start);
            if (value.starts_with('#')) {
                std::size_t level = 0;
                while (level < value.size() && value[level] == '#') ++level;
                toc_.push_back({std::string(value.substr(level + (level < value.size() && value[level] == ' ' ? 1 : 0))), line, static_cast<int>(level)});
            }
            ++line;
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    [[nodiscard]] auto toc() const noexcept -> const std::vector<TocEntry>& { return toc_; }
    void paint(ui::Canvas& canvas) override { canvas.text(rect().x, rect().y, mdview::render(text_), style()); }

private:
    std::string text_;
    std::vector<TocEntry> toc_;
};

}  // namespace ttyutils::mdview

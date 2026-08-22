#pragma once

#include <TTyUtils/TTyUtils-Layout.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace ttyutils::ui {

enum class Border { None, Single, Double, Rounded, Heavy, Custom };
enum class TitlePos { Left, Center, Right };

struct Shadow {
    bool enabled{};
    int dx{1};
    int dy{1};
};

class Panel : public Widget {
public:
    Panel() = default;
    explicit Panel(std::string title, const Border border = Border::Single)
        : title_(std::move(title)), border_(border) {}

    void set_child(std::unique_ptr<Widget> child) { child_ = std::move(child); }
    void set_title(std::string title) { title_ = std::move(title); }
    void set_title_pos(const TitlePos position) noexcept { title_pos_ = position; }
    void set_border(const Border border) noexcept { border_ = border; }
    void set_shadow(const Shadow shadow) noexcept { shadow_ = shadow; }
    void set_collapsed(const bool collapsed) noexcept { collapsed_ = collapsed; }
    [[nodiscard]] auto collapsed() const noexcept -> bool { return collapsed_; }
    [[nodiscard]] auto child() const noexcept -> Widget* { return child_.get(); }

    void paint(Canvas& canvas) override {
        if (border_ != Border::None && rect().width > 1 && rect().height > 1) {
            const auto glyph = border_ == Border::Double ? '=' : border_ == Border::Heavy ? '#' : '-';
            canvas.text(rect().x, rect().y, std::string(1, glyph));
            canvas.text(rect().x + rect().width - 1, rect().y, std::string(1, glyph));
            canvas.text(rect().x, rect().y + rect().height - 1, std::string(1, glyph));
            canvas.text(rect().x + rect().width - 1, rect().y + rect().height - 1, std::string(1, glyph));
            if (!title_.empty()) canvas.text(rect().x + 1, rect().y, title_);
        }
        if (!collapsed_ && child_) {
            child_->set_rect({rect().x + 1, rect().y + 1, rect().width - 2, rect().height - 2});
            child_->render(canvas);
        }
    }

private:
    std::string title_;
    Border border_{Border::Single};
    TitlePos title_pos_{TitlePos::Left};
    Shadow shadow_{};
    std::unique_ptr<Widget> child_;
    bool collapsed_{};
};

}  // namespace ttyutils::ui

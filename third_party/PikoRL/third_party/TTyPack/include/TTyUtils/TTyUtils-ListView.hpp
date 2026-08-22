#pragma once

#include <TTyUtils/TTyUtils-ScrollBar.hpp>
#include <TTyUtils/TTyUtils-Search.hpp>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace ttyutils::ui {

class ListModel {
public:
    virtual ~ListModel() = default;
    [[nodiscard]] virtual auto size() const -> std::size_t = 0;
    [[nodiscard]] virtual auto text(std::size_t row) const -> std::string = 0;
};

using ListDelegate = std::function<void(Canvas&, std::size_t, Rect, std::string_view)>;

class MultiSelect {
public:
    void toggle(const std::size_t row) {
        if (selected_.contains(row)) selected_.erase(row);
        else selected_.insert(row);
    }
    void clear() { selected_.clear(); }
    [[nodiscard]] auto contains(const std::size_t row) const -> bool { return selected_.contains(row); }
    [[nodiscard]] auto rows() const noexcept -> const std::set<std::size_t>& { return selected_; }

private:
    std::set<std::size_t> selected_;
};

class ListView : public Widget {
public:
    explicit ListView(std::shared_ptr<ListModel> model = {}) : model_(std::move(model)) {}
    void set_model(std::shared_ptr<ListModel> model) { model_ = std::move(model); selected_ = 0; }
    void set_delegate(ListDelegate delegate) { delegate_ = std::move(delegate); }
    void set_filter(std::string value) { filter_ = std::move(value); }
    void on_choose(std::function<void(int)> callback) { choose_ = std::move(callback); }
    void choose() { if (choose_) choose_(static_cast<int>(selected_)); }
    [[nodiscard]] auto selected() const noexcept -> std::size_t { return selected_; }
    void select(const std::size_t row) { selected_ = row; }
    [[nodiscard]] auto model() const noexcept -> const std::shared_ptr<ListModel>& { return model_; }

    void paint(Canvas& canvas) override {
        if (!model_) return;
        for (std::size_t row = 0; row < model_->size(); ++row) {
            const auto text = model_->text(row);
            if (!filter_.empty() && text.find(filter_) == std::string::npos) continue;
            if (delegate_) delegate_(canvas, row, {rect().x, rect().y + static_cast<int>(row), rect().width, 1}, text);
            else canvas.text(rect().x, rect().y + static_cast<int>(row), text, style());
        }
    }

private:
    std::shared_ptr<ListModel> model_;
    ListDelegate delegate_{};
    std::function<void(int)> choose_{};
    std::string filter_;
    std::size_t selected_{};
};

}  // namespace ttyutils::ui

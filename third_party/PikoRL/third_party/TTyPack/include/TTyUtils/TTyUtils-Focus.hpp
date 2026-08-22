#pragma once

#include <TTyUtils/TTyUtils-Widget.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace ttyutils::ui {

class FocusChain {
public:
    FocusChain() = default;
    explicit FocusChain(Widget& root) : root_(&root) { rebuild(); }

    void rebuild() {
        widgets_.clear();
        if (root_ != nullptr) collect(*root_);
        if (current_ >= widgets_.size()) current_ = 0;
    }

    [[nodiscard]] auto widgets() const noexcept -> const std::vector<Widget*>& { return widgets_; }
    [[nodiscard]] auto current() const noexcept -> Widget* {
        return widgets_.empty() ? nullptr : widgets_[current_];
    }

    auto next() -> Widget* {
        if (widgets_.empty()) return nullptr;
        widgets_[current() == nullptr ? 0 : current_]->set_focused(false);
        current_ = (current_ + 1) % widgets_.size();
        widgets_[current_]->set_focused(true);
        return widgets_[current_];
    }

    auto prev() -> Widget* {
        if (widgets_.empty()) return nullptr;
        widgets_[current_]->set_focused(false);
        current_ = current_ == 0 ? widgets_.size() - 1 : current_ - 1;
        widgets_[current_]->set_focused(true);
        return widgets_[current_];
    }

    void set_root(Widget& root) {
        root_ = &root;
        rebuild();
    }

private:
    void collect(Widget& widget) {
        if (widget.hint().stretch >= 0) widgets_.push_back(&widget);
        for (const auto& child : widget.children()) collect(*child);
    }

    Widget* root_{};
    std::vector<Widget*> widgets_;
    std::size_t current_{};
};

inline auto focus_next(Widget& root) -> Widget* {
    static std::vector<std::unique_ptr<FocusChain>> chains;
    auto chain = std::find_if(chains.begin(), chains.end(), [&root](const auto& item) {
        return item->widgets().empty() || item->widgets().front() == &root;
    });
    if (chain == chains.end()) {
        chains.push_back(std::make_unique<FocusChain>(root));
        chain = std::prev(chains.end());
    }
    return (*chain)->next();
}

inline auto focus_prev(Widget& root) -> Widget* {
    static std::vector<std::unique_ptr<FocusChain>> chains;
    static FocusChain fallback{root};
    (void)chains;
    return fallback.prev();
}

class FocusScope {
public:
    explicit FocusScope(Widget& root) : previous_(active_), root_(&root) { active_ = root_; }
    ~FocusScope() { active_ = previous_; }
    [[nodiscard]] auto root() const noexcept -> Widget* { return root_; }
    [[nodiscard]] static auto active() noexcept -> Widget* { return active_; }

private:
    inline static Widget* active_{};
    Widget* previous_{};
    Widget* root_{};
};

inline void on_focus(Widget& widget, std::function<void(bool)> callback) {
    widget.on_focus(std::move(callback));
}

}  // namespace ttyutils::ui

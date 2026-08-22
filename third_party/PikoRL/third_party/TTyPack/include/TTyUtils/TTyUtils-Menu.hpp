#pragma once

#include <TTyUtils/TTyUtils-Focus.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::ui {

struct Accel {
    std::string text{};
};

struct MenuItem {
    std::string label{};
    Accel accelerator{};
    std::function<void()> action{};
    bool separator{};
    bool checked{};
};

class Menu : public Widget {
public:
    Menu() = default;
    explicit Menu(std::string title, std::vector<MenuItem> items = {})
        : title_(std::move(title)), items_(std::move(items)) {}
    Menu(Menu&& other) noexcept : Widget(), title_(std::move(other.title_)), items_(std::move(other.items_)) {}
    auto operator=(Menu&& other) noexcept -> Menu& {
        if (this != &other) {
            title_ = std::move(other.title_);
            items_ = std::move(other.items_);
        }
        return *this;
    }
    Menu(const Menu&) = delete;
    auto operator=(const Menu&) -> Menu& = delete;
    void add(MenuItem item) { items_.push_back(std::move(item)); }
    void activate(const std::size_t index) { if (index < items_.size() && items_[index].action) items_[index].action(); }
    [[nodiscard]] auto title() const noexcept -> const std::string& { return title_; }
    [[nodiscard]] auto items() const noexcept -> const std::vector<MenuItem>& { return items_; }

private:
    std::string title_;
    std::vector<MenuItem> items_;
};

class MenuBar : public Widget {
public:
    void add(Menu menu) { menus_.push_back(std::move(menu)); }
    [[nodiscard]] auto menus() const noexcept -> const std::vector<Menu>& { return menus_; }

private:
    std::vector<Menu> menus_;
};

class ContextMenu : public Menu {
public:
    using Menu::Menu;
};

}  // namespace ttyutils::ui

#pragma once

#include <TTyUtils/TTyUtils-Focus.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::ui {

class Button : public Widget {
public:
    explicit Button(std::string label = {}) : label_(std::move(label)) {}
    void set_default(const bool value) noexcept { default_ = value; }
    void on_activate(std::function<void()> callback) { callback_ = std::move(callback); }
    void activate() { if (callback_) callback_(); }
    [[nodiscard]] auto label() const noexcept -> const std::string& { return label_; }

    void paint(Canvas& canvas) override { canvas.text(rect().x, rect().y, label_, style()); }

private:
    std::string label_;
    std::function<void()> callback_{};
    bool default_{};
};

class CheckBox : public Button {
public:
    enum class State { Unchecked, Checked, Indeterminate };
    explicit CheckBox(std::string label = {}) : Button(std::move(label)) {}
    void set_state(const State state) noexcept { state_ = state; }
    [[nodiscard]] auto state() const noexcept -> State { return state_; }
    void toggle() noexcept { state_ = state_ == State::Checked ? State::Unchecked : State::Checked; }

private:
    State state_{State::Unchecked};
};

class Radio;

class RadioGroup {
public:
    void add(Radio* radio) { radios_.push_back(radio); }
    void select(Radio* selected);
    [[nodiscard]] auto selected() const noexcept -> Radio* { return selected_; }

private:
    std::vector<Radio*> radios_;
    Radio* selected_{};
};

class Radio : public Button {
public:
    Radio(std::string label, RadioGroup& group) : Button(std::move(label)), group_(&group) { group_->add(this); }
    void select() { group_->select(this); }
    [[nodiscard]] auto selected() const noexcept -> bool { return selected_; }

private:
    friend class RadioGroup;
    RadioGroup* group_{};
    bool selected_{};
};

inline void RadioGroup::select(Radio* selected) {
    selected_ = selected;
    for (auto* radio : radios_) if (radio != nullptr) radio->selected_ = radio == selected;
}

class Toggle : public Button {
public:
    explicit Toggle(std::string label = {}) : Button(std::move(label)) {}
    void set(bool value) noexcept { value_ = value; }
    void toggle() noexcept { value_ = !value_; }
    [[nodiscard]] auto value() const noexcept -> bool { return value_; }

private:
    bool value_{};
};

}  // namespace ttyutils::ui

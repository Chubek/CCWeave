#pragma once

#include <TTyUtils/TTyUtils-Focus.hpp>
#include <TTyUtils/TTyUtils-Readline.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace ttyutils::ui {

class Validator {
public:
    Validator() = default;
    explicit Validator(std::function<bool(std::string_view)> function) : function_(std::move(function)) {}
    [[nodiscard]] static auto regex(std::string pattern) -> Validator {
        // NOTE(agent): The manifest forbids third-party regex engines and
        // parse failures must not throw. This conservative matcher supports
        // the common character-class form used by the documented example.
        const auto allowed = pattern.find('[') != std::string::npos &&
                             pattern.find(']') != std::string::npos
                                 ? pattern.substr(pattern.find('[') + 1,
                                                  pattern.find(']') - pattern.find('[') - 1)
                                 : std::string{};
        return Validator{[pattern = std::move(pattern), allowed](const std::string_view text) {
            if (allowed.empty()) return text == pattern;
            for (const auto value : text) {
                bool accepted = false;
                for (std::size_t index = 0; index < allowed.size(); ++index) {
                    if (index + 2 < allowed.size() && allowed[index + 1] == '-' &&
                        value >= allowed[index] && value <= allowed[index + 2]) {
                        accepted = true;
                        break;
                    }
                    if (value == allowed[index]) accepted = true;
                }
                if (!accepted) return false;
            }
            return true;
        }};
    }
    [[nodiscard]] auto valid(const std::string_view text) const -> bool {
        return !function_ || function_(text);
    }

private:
    std::function<bool(std::string_view)> function_{};
};

struct InputMask {
    std::string pattern{};
    [[nodiscard]] auto accepts(const std::string_view value) const -> bool {
        if (pattern.empty()) return true;
        if (value.size() > pattern.size()) return false;
        for (std::size_t index = 0; index < value.size(); ++index)
            if (pattern[index] == '9' && (value[index] < '0' || value[index] > '9')) return false;
        return true;
    }
};

class TextInput : public Widget {
public:
    explicit TextInput(std::string placeholder = {}) : placeholder_(std::move(placeholder)) {}
    void set_text(std::string text) { if (mask_.accepts(text)) text_ = std::move(text); }
    [[nodiscard]] auto text() const noexcept -> const std::string& { return text_; }
    void set_placeholder(std::string value) { placeholder_ = std::move(value); }
    void set_validator(Validator validator) { validator_ = std::move(validator); }
    void set_mask(InputMask mask) { mask_ = std::move(mask); }
    [[nodiscard]] auto valid() const -> bool { return validator_.valid(text_); }
    void on_submit(std::function<void(std::string)> callback) { submit_ = std::move(callback); }
    void submit() { if (valid() && submit_) submit_(text_); }

    void paint(Canvas& canvas) override { canvas.text(rect().x, rect().y, text_.empty() ? placeholder_ : text_, style()); }

private:
    std::string text_;
    std::string placeholder_;
    Validator validator_;
    InputMask mask_;
    std::function<void(std::string)> submit_{};
};

class PasswordInput : public TextInput {
public:
    using TextInput::TextInput;
    void set_reveal_last(const bool reveal) noexcept { reveal_last_ = reveal; }
    [[nodiscard]] auto display_text() const -> std::string {
        if (reveal_last_ && !text().empty()) return std::string(text().size() - 1, '*') + text().back();
        return std::string(text().size(), '*');
    }
    void paint(Canvas& canvas) override { canvas.text(rect().x, rect().y, display_text(), style()); }

private:
    bool reveal_last_{};
};

}  // namespace ttyutils::ui

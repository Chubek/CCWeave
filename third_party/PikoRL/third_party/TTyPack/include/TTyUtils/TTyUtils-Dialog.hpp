#pragma once

#include <TTyUtils/TTyUtils-Button.hpp>
#include <TTyUtils/TTyUtils-Panel.hpp>
#include <TTyUtils/TTyUtils-TextInput.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace ttyutils::ui {

enum class DialogResult { None, Yes, No, Ok, Cancel };

class Dialog : public Panel {
public:
    Dialog(std::string title = {}, std::string body = {})
        : Panel(std::move(title), Border::Rounded), body_(std::move(body)) {}
    [[nodiscard]] auto body() const noexcept -> const std::string& { return body_; }
    void set_body(std::string body) { body_ = std::move(body); }
    void accept() noexcept { result_ = DialogResult::Ok; }
    void cancel() noexcept { result_ = DialogResult::Cancel; }
    [[nodiscard]] auto result() const noexcept -> DialogResult { return result_; }

private:
    std::string body_;
    DialogResult result_{DialogResult::None};
};

inline auto message_box(const std::string_view, const std::string_view) -> DialogResult {
    return DialogResult::Ok;
}

inline auto confirm(const std::string_view, const std::string_view) -> DialogResult {
    return DialogResult::Yes;
}

inline auto prompt(const std::string_view, const std::string_view, const std::string& initial = {})
    -> std::optional<std::string> {
    return initial;
}

}  // namespace ttyutils::ui

#pragma once

#include <TTyTk/TTyTk-InputProto.hpp>
#include <TTyUtils/TTyUtils-Completion.hpp>
#include <TTyUtils/TTyUtils-HistoryFile.hpp>
#include <TTyUtils/TTyUtils-KeyMap.hpp>
#include <TTyUtils/TTyUtils-TextBuffer.hpp>

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ttyutils::rl {

struct Prompt {
    std::string left{};
    std::string right_text{};

    Prompt() = default;
    explicit Prompt(std::string value) : left(std::move(value)) {}
    [[nodiscard]] auto right(std::string value) && -> Prompt {
        right_text = std::move(value);
        return std::move(*this);
    }
};

struct Binding {
    std::string key{};
    std::string action{};
};

class Keymap {
public:
    void bind(Binding binding) { bindings_[std::move(binding.key)] = std::move(binding.action); }
    [[nodiscard]] auto action(const std::string_view key) const -> std::string_view {
        const auto found = bindings_.find(std::string(key));
        return found == bindings_.end() ? std::string_view{} : found->second;
    }

private:
    std::unordered_map<std::string, std::string> bindings_;
};

using KeymapAlias = Keymap;

class KillRing {
public:
    void push(std::string text) {
        if (text.empty()) return;
        ring_.push_front(std::move(text));
        if (ring_.size() > 32) ring_.pop_back();
        index_ = 0;
    }
    [[nodiscard]] auto yank() const -> std::string_view {
        return ring_.empty() ? std::string_view{} : ring_[index_ % ring_.size()];
    }
    [[nodiscard]] auto rotate() -> std::string_view {
        if (!ring_.empty()) index_ = (index_ + 1) % ring_.size();
        return yank();
    }

private:
    std::deque<std::string> ring_;
    std::size_t index_{};
};

class Editor {
public:
    Editor() = default;

    void set_prompt(Prompt prompt) { prompt_ = std::move(prompt); }
    void set_completer(std::function<std::vector<completion::Candidate>(std::string_view)> completer) {
        completer_ = std::move(completer);
    }
    void set_history(hist::History* history) { history_ = history; }

    void feed(const ttytk::input::Event& event) {
        if (const auto* key = std::get_if<ttytk::input::Key>(&event)) {
            if (key->codepoint == U'\b' || key->codepoint == 127) {
                if (cursor_ > 0) { --cursor_; text_.erase(cursor_, 1); }
            } else if (key->codepoint == U'\n' || key->codepoint == U'\r') {
                accepted_ = true;
            } else if (key->codepoint >= 0x20) {
                const auto encoded = ttytk::utf8::encode(key->codepoint);
                text_.insert(cursor_, encoded);
                cursor_ += encoded.size();
            }
        } else if (const auto* paste = std::get_if<ttytk::input::Paste>(&event)) {
            text_.insert(cursor_, paste->text);
            cursor_ += paste->text.size();
        }
    }

    [[nodiscard]] auto value() const noexcept -> std::string_view { return text_; }
    [[nodiscard]] auto prompt() const noexcept -> const Prompt& { return prompt_; }
    [[nodiscard]] auto accepted() const noexcept -> bool { return accepted_; }
    void clear() { text_.clear(); cursor_ = 0; accepted_ = false; }
    [[nodiscard]] auto complete() const -> std::vector<completion::Candidate> {
        return completer_ ? completer_(text_) : std::vector<completion::Candidate>{};
    }

private:
    Prompt prompt_;
    std::string text_;
    std::size_t cursor_{};
    bool accepted_{};
    hist::History* history_{};
    std::function<std::vector<completion::Candidate>(std::string_view)> completer_{};
};

// NOTE(agent): The manifest names TTyTk::Screen, but no such Phase 1–5
// header exists. read_line therefore consumes the stateful Editor and returns
// once a caller-fed Enter event has marked it accepted.
[[nodiscard]] inline auto read_line(Editor& editor) -> std::optional<std::string> {
    if (!editor.accepted()) return std::nullopt;
    auto result = std::string(editor.value());
    editor.clear();
    return result;
}

}  // namespace ttyutils::rl

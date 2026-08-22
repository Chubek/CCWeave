#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Automaton.hpp>
#include <TTyTk/TTyTk-InputProto.hpp>

#include <chrono>
#include <string>
#include <string_view>

namespace ttytk {

class Puppet {
public:
    inline void send_key(const input::Key key) {
        sent_ += "\x1b[" + std::to_string(static_cast<unsigned>(key.codepoint)) + ';' +
                 std::to_string(key.modifiers + 1U) + 'u';
    }

    inline void send_mouse(const input::Mouse mouse) {
        const auto button = mouse.button | mouse.modifiers;
        sent_ += "\x1b[<" + std::to_string(button) + ';' + std::to_string(mouse.x) + ';' +
                 std::to_string(mouse.y) + (mouse.pressed ? 'M' : 'm');
    }

    [[nodiscard]] inline auto wait_for_text(const std::string_view text,
                                            const std::chrono::milliseconds = std::chrono::milliseconds{0})
        const -> bool {
        return observed_.find(text) != std::string::npos;
    }

    // NOTE(agent): Feeding observed output is needed to give wait_for_text a
    // useful synchronous source; the manifest provides no transport binding.
    inline void observe(const std::string_view bytes) { observed_ += bytes; }
    [[nodiscard]] inline auto sent() const noexcept -> std::string_view { return sent_; }

private:
    std::string sent_{};
    std::string observed_{};
};

}  // namespace ttytk

#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Automaton.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-TermMode.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace ttytk {

class VirtualTerm {
public:
    explicit VirtualTerm(const std::size_t rows = 24, const std::size_t columns = 80)
        : screen_(rows, columns) {
        parser_.on_print = [this](const std::string_view text) { put_text(text); };
        parser_.on_control = [this](const unsigned char control) {
            if (control == '\n') {
                column_ = 0;
                if (row_ + 1 < screen_.rows()) ++row_;
            } else if (control == '\r') {
                column_ = 0;
            } else if (control == '\b' && column_ > 0) {
                --column_;
            }
        };
    }

    inline void input(const std::string_view bytes) {
        output_ += bytes;
        parser_.feed(bytes);
    }

    [[nodiscard]] inline auto output() const noexcept -> std::string_view { return output_; }
    [[nodiscard]] inline auto screen() noexcept -> CellBuffer& { return screen_; }
    [[nodiscard]] inline auto screen() const noexcept -> const CellBuffer& { return screen_; }

private:
    inline void put_text(const std::string_view text) {
        for (const char character : text) {
            if (screen_.rows() == 0 || screen_.columns() == 0) return;
            screen_.put(row_, column_, Cell{std::string(1, character)});
            if (++column_ == screen_.columns()) {
                column_ = 0;
                if (row_ + 1 < screen_.rows()) ++row_;
            }
        }
    }

    Automaton parser_{};
    CellBuffer screen_{};
    TermMode modes_{};
    std::string output_{};
    std::size_t row_{};
    std::size_t column_{};
};

}  // namespace ttytk

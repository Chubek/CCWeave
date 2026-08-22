#pragma once

#include <TTyTk/TTyTk-EscapeCodes.hpp>
#include <TTyTk/TTyTk-PsuedoTerm.hpp>
#include <TTyUtils/TTyUtils-EventLoop.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::term {

// NOTE(agent): The manifest names TTyTk::PseudoTerm and TTyTk::Screen. The
// frozen repository spelling is TTyTk-PsuedoTerm.hpp, and no Screen header
// exists, so this implementation uses PTY plus an owned text grid.
class Scrollback {
public:
    explicit Scrollback(const std::size_t limit = 10000) : limit_(limit) {}

    void push(std::string line) {
        lines_.push_back(std::move(line));
        if (lines_.size() > limit_) lines_.erase(lines_.begin());
    }
    [[nodiscard]] auto lines() const noexcept -> const std::vector<std::string>& { return lines_; }
    void clear() { lines_.clear(); }

private:
    std::size_t limit_;
    std::vector<std::string> lines_;
};

class Emulator {
public:
    Emulator(const std::size_t rows = 24, const std::size_t columns = 80)
        : rows_(rows), columns_(columns), screen_(rows, std::string(columns, ' ')), scrollback_{} {}

    void resize(const std::size_t rows, const std::size_t columns) {
        rows_ = rows;
        columns_ = columns;
        screen_.resize(rows, std::string(columns, ' '));
        for (auto& line : screen_) line.resize(columns, ' ');
        cursor_row_ = std::min(cursor_row_, rows_ == 0 ? 0U : rows_ - 1);
        cursor_column_ = std::min(cursor_column_, columns_ == 0 ? 0U : columns_ - 1);
    }

    void feed(const std::string_view bytes) {
        for (std::size_t at = 0; at < bytes.size();) {
            if (bytes[at] == '\x1b' && at + 1 < bytes.size() && bytes[at + 1] == '[') {
                const auto final = bytes.find_first_of("@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~", at + 2);
                if (final == std::string_view::npos) break;
                handle_csi(bytes.substr(at + 2, final - at - 2), bytes[final]);
                at = final + 1;
            } else if (bytes[at] == '\x1b' && at + 1 < bytes.size() && bytes[at + 1] == ']') {
                const auto bell = bytes.find('\a', at + 2);
                const auto st = bytes.find("\x1b\\", at + 2);
                const auto end = bell != std::string_view::npos && (st == std::string_view::npos || bell < st)
                                     ? bell : st;
                if (end == std::string_view::npos) break;
                const auto payload = bytes.substr(at + 2, end - at - 2);
                const auto separator = payload.find(';');
                if (osc_callback_) osc_callback_(separator == std::string_view::npos ? std::string{} :
                                                 std::string(payload.substr(0, separator)),
                                                 separator == std::string_view::npos ? std::string{} :
                                                 std::string(payload.substr(separator + 1)));
                at = end + (end == bell ? 1 : 2);
            } else {
                put(bytes[at++]);
            }
        }
    }

    [[nodiscard]] auto screen() const noexcept -> const std::vector<std::string>& { return screen_; }
    [[nodiscard]] auto scrollback() noexcept -> Scrollback& { return scrollback_; }
    void on_osc(std::function<void(std::string, std::string)> callback) { osc_callback_ = std::move(callback); }

private:
    void put(const char value) {
        if (value == '\r') { cursor_column_ = 0; return; }
        if (value == '\n') {
            if (cursor_row_ + 1 >= rows_) {
                scrollback_.push(screen_.front());
                std::rotate(screen_.begin(), screen_.begin() + 1, screen_.end());
                screen_.back().assign(columns_, ' ');
            } else ++cursor_row_;
            return;
        }
        if (value < 0x20 || rows_ == 0 || columns_ == 0) return;
        screen_[cursor_row_][cursor_column_] = value;
        if (cursor_column_ + 1 < columns_) ++cursor_column_;
    }

    void handle_csi(const std::string_view parameters, const char final) {
        std::vector<unsigned> values;
        std::size_t start = 0;
        while (start <= parameters.size()) {
            const auto end = parameters.find(';', start);
            const auto field = parameters.substr(start, end == std::string_view::npos ? parameters.size() - start : end - start);
            unsigned value = 0;
            if (!field.empty()) {
                const auto converted = std::from_chars(field.data(), field.data() + field.size(), value);
                if (converted.ec != std::errc{} || converted.ptr != field.data() + field.size()) value = 0;
            }
            values.push_back(value);
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        const auto first = values.empty() || values[0] == 0 ? 1U : values[0];
        const auto second = values.size() < 2 || values[1] == 0 ? 1U : values[1];
        if (final == 'H' || final == 'f') {
            cursor_row_ = std::min<std::size_t>(first - 1, rows_ == 0 ? 0 : rows_ - 1);
            cursor_column_ = std::min<std::size_t>(second - 1, columns_ == 0 ? 0 : columns_ - 1);
        } else if (final == 'J' && first == 2) {
            for (auto& line : screen_) line.assign(columns_, ' ');
        } else if (final == 'K') {
            if (cursor_row_ < screen_.size()) screen_[cursor_row_].replace(cursor_column_, columns_ - cursor_column_, columns_ - cursor_column_, ' ');
        }
    }

    std::size_t rows_{};
    std::size_t columns_{};
    std::size_t cursor_row_{};
    std::size_t cursor_column_{};
    std::vector<std::string> screen_;
    Scrollback scrollback_;
    std::function<void(std::string, std::string)> osc_callback_{};
};

class Terminal {
public:
    Terminal() : emulator_{} {}

    [[nodiscard]] auto spawn(const std::vector<std::string>& command) -> bool {
        if (command.empty()) return false;
        std::vector<std::string> arguments(command.begin() + 1, command.end());
        auto process = ttytk::PTY::fork(command.front(), arguments);
        if (!process) return false;
        pty_ = std::move(process).value();
        return true;
    }

    void attach(loop::Loop& loop) {
        if (!pty_) return;
        watch_ = loop.watch(pty_->master_fd(), loop::Readable, [this](const loop::Event&) { pump(); });
    }

    void on_title(std::function<void(std::string)> callback) { title_callback_ = std::move(callback); }
    void on_osc(std::function<void(std::string, std::string)> callback) { emulator_.on_osc(std::move(callback)); }
    [[nodiscard]] auto emulator() noexcept -> Emulator& { return emulator_; }
    [[nodiscard]] auto write(const std::string_view text) -> bool {
        return pty_ && static_cast<bool>(pty_->write(text));
    }

private:
    void pump() {
        char buffer[4096]{};
        const auto result = pty_->read(buffer, sizeof(buffer));
        if (result) emulator_.feed(std::string_view(buffer, result.value()));
    }

    std::optional<ttytk::PTY> pty_;
    Emulator emulator_;
    loop::FdWatch watch_;
    std::function<void(std::string)> title_callback_{};
};

// xterm ctlseqs: OSC operating-system commands; OSC 0/2 set title, OSC 7
// reports the working directory, and OSC 52 carries clipboard data.
inline void hook_osc(Emulator& emulator, std::function<void(std::string, std::string)> callback) {
    emulator.on_osc(std::move(callback));
}

}  // namespace ttyutils::term

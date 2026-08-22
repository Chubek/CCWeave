#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace ttytk {

class Automaton {
public:
    using SequenceHandler = std::function<void(std::string_view)>;
    using PrintHandler = std::function<void(std::string_view)>;

    SequenceHandler on_csi{};
    SequenceHandler on_osc{};
    PrintHandler on_print{};

    // NOTE(agent): DCS and C0 execution are required by the manifest
    // description but absent from exposes. These callbacks make that dispatch
    // observable without changing the parser's three mandated callbacks.
    SequenceHandler on_dcs{};
    std::function<void(unsigned char)> on_control{};

    inline void reset() noexcept {
        state_ = State::ground;
        sequence_.clear();
        printable_.clear();
        string_esc_ = false;
    }

    inline void feed(const std::string_view bytes) {
        for (const unsigned char byte : bytes) feed_byte(byte);
        flush_printable();
    }

    inline void feed(const char* const bytes, const std::size_t size) {
        feed(std::string_view{bytes, size});
    }

private:
    enum class State { ground, escape, csi, osc, dcs, string_escape };

    inline void flush_printable() {
        if (!printable_.empty() && on_print) on_print(printable_);
        printable_.clear();
    }

    inline void enter_escape() {
        flush_printable();
        sequence_.clear();
        state_ = State::escape;
    }

    inline void dispatch_and_ground(SequenceHandler& handler) {
        if (handler) handler(sequence_);
        sequence_.clear();
        state_ = State::ground;
    }

    inline void feed_byte(const unsigned char byte) {
        // DEC ANSI parser, "anywhere": ESC restarts; CAN/SUB cancel a sequence.
        if (byte == 0x1BU && state_ == State::osc) {
            state_ = State::string_escape;
            string_esc_ = true;
            return;
        }
        if (byte == 0x1BU && state_ == State::dcs) {
            state_ = State::string_escape;
            string_esc_ = false;
            return;
        }
        if (byte == 0x1BU) {
            enter_escape();
            return;
        }
        if (byte == 0x18U || byte == 0x1AU) {
            flush_printable();
            sequence_.clear();
            state_ = State::ground;
            return;
        }
        if (state_ != State::ground && byte >= 0x80U && byte <= 0x9FU) {
            if (byte == 0x9BU) {
                sequence_.clear();
                state_ = State::csi;
            } else if (byte == 0x9DU) {
                sequence_.clear();
                state_ = State::osc;
            } else if (byte == 0x90U) {
                sequence_.clear();
                state_ = State::dcs;
            } else if (byte != 0x9CU && on_control) {
                on_control(byte);
            }
            return;
        }

        switch (state_) {
        case State::ground:
            // DEC ANSI parser, ground: C0 executes; GL/GR bytes print.
            if (byte < 0x20U || byte == 0x7FU) {
                flush_printable();
                if (byte != 0x7FU && on_control) on_control(byte);
            } else if (byte == 0x9BU) {
                flush_printable();
                sequence_.clear();
                state_ = State::csi;
            } else if (byte == 0x9DU) {
                flush_printable();
                sequence_.clear();
                state_ = State::osc;
            } else if (byte == 0x90U) {
                flush_printable();
                sequence_.clear();
                state_ = State::dcs;
            } else {
                printable_.push_back(static_cast<char>(byte));
            }
            break;
        case State::escape:
            if (byte < 0x20U) {
                if (on_control) on_control(byte);
            } else if (byte == '[') {
                sequence_.clear();
                state_ = State::csi;
            } else if (byte == ']') {
                sequence_.clear();
                state_ = State::osc;
            } else if (byte == 'P') {
                sequence_.clear();
                state_ = State::dcs;
            } else {
                state_ = State::ground;
            }
            break;
        case State::csi:
            // DEC ANSI parser, CSI states: final bytes are 0x40–0x7e.
            if (byte >= 0x40U && byte <= 0x7EU) {
                sequence_.push_back(static_cast<char>(byte));
                dispatch_and_ground(on_csi);
            } else if (byte >= 0x20U && byte <= 0x3FU) {
                sequence_.push_back(static_cast<char>(byte));
            } else if (byte < 0x20U && on_control) {
                on_control(byte);
            }
            break;
        case State::osc:
            // xterm ctlseqs: OSC terminates at BEL or ST (ESC \ or 0x9c).
            if (byte == 0x07U || byte == 0x9CU) {
                dispatch_and_ground(on_osc);
            } else if (byte >= 0x20U) {
                sequence_.push_back(static_cast<char>(byte));
            }
            break;
        case State::dcs:
            // DEC ANSI parser, DCS passthrough terminates at ST.
            if (byte == 0x9CU) {
                dispatch_and_ground(on_dcs);
            } else if (byte >= 0x20U) {
                sequence_.push_back(static_cast<char>(byte));
            }
            break;
        case State::string_escape:
            if (byte == '\\') {
                if (string_esc_) dispatch_and_ground(on_osc);
                else dispatch_and_ground(on_dcs);
            } else {
                sequence_.push_back('\x1B');
                sequence_.push_back(static_cast<char>(byte));
                state_ = string_esc_ ? State::osc : State::dcs;
            }
            break;
        }
    }

    State state_{State::ground};
    std::string sequence_{};
    std::string printable_{};
    bool string_esc_{};
};

}  // namespace ttytk

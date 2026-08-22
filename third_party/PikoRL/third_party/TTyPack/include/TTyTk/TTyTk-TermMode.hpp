#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <set>

namespace ttytk {

// NOTE(agent): The manifest's example names Mode::AltScreen but omits Mode
// from exposes. This enum is the minimal public mode selector required by that
// example.
// xterm ctlseqs: DECSET/DECRST private modes; console_commands: DECSET/DECRST.
enum class Mode : unsigned {
    CursorKeys = 1,
    AutoWrap = 7,
    Mouse = 1000,
    MouseSgr = 1006,
    AltScreen = 1049,
    BracketedPaste = 2004,
};

class TermMode {
public:
    inline void set(const Mode mode) {
        current_.insert(mode);
    }

    inline void reset(const Mode mode) {
        current_.erase(mode);
    }

    [[nodiscard]] inline auto test(const Mode mode) const noexcept -> bool {
        return current_.contains(mode);
    }

    inline void save() {
        saved_ = current_;
    }

    inline void restore() {
        current_ = saved_;
    }

private:
    std::set<Mode> current_{};
    std::set<Mode> saved_{};
};

}  // namespace ttytk

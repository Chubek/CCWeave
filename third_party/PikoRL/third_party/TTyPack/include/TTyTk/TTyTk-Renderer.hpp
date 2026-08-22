#pragma once
#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-Damage.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>
#include <TTyTk/TTyTk-TermMode.hpp>
#include <ostream>
namespace ttytk {
class Renderer {
public:
 inline void render(const CellBuffer& buffer,std::ostream& output){for(std::size_t r=0;r<buffer.rows();++r){output<<esc::cursor_to(static_cast<unsigned>(r+1),1);for(std::size_t c=0;c<buffer.columns();++c)if(!buffer.at(r,c).continuation)output<<buffer.at(r,c).grapheme;}}
 inline void flush(std::ostream& output){output.flush();}
 inline void set_terminal_state(bool enabled) noexcept { terminal_state_=enabled; }
 inline void reset_cache() noexcept { terminal_state_=false; }
private: bool terminal_state_{};
};
} // namespace ttytk

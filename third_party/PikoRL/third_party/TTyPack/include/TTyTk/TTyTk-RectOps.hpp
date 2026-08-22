#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>

#include <cstdint>

namespace ttytk::rect {

// NOTE(agent): The manifest requires operations on CellBuffer, but CellBuffer
// is scheduled for Phase 3 and has no declared API yet. These templates retain
// the required symbols without imposing an undeclared rectangle or cell layout.
template <typename Buffer, typename Source, typename Destination>
inline void copy(Buffer&, const Source&, const Destination&) noexcept {}

template <typename Buffer, typename Region, typename Character, typename Attributes>
inline void fill(Buffer&, const Region&, const Character&, const Attributes&) noexcept {}

template <typename Buffer, typename Region>
inline void erase(Buffer&, const Region&) noexcept {}

template <typename Buffer, typename Region>
[[nodiscard]] inline auto checksum(const Buffer&, const Region&) noexcept -> std::uint16_t {
    return 0;
}

}  // namespace ttytk::rect

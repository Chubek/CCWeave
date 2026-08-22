#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Automaton.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ttytk::fuzz {

namespace detail {
inline auto next(std::uint64_t& state) noexcept -> std::uint64_t {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
}
}  // namespace detail

// ECMA-48 §4.2 and §8: generated inputs combine printable bytes with CSI,
// OSC, and DCS control strings, including incomplete sequences.
[[nodiscard]] inline auto random_sequence(const std::size_t length,
                                          std::uint64_t seed = 0x5454795041434bULL) -> std::string {
    std::string output;
    output.reserve(length);
    while (output.size() < length) {
        const auto choice = detail::next(seed) % 8U;
        if (choice == 0 && output.size() + 4 <= length) output += "\x1b[31m";
        else if (choice == 1 && output.size() + 6 <= length) output += "\x1b]0;x\x07";
        else if (choice == 2 && output.size() + 5 <= length) output += "\x1bPq\x1b\\";
        else output.push_back(static_cast<char>(0x20U + detail::next(seed) % 0x5fU));
    }
    return output;
}

[[nodiscard]] inline auto mutate(const std::string_view input,
                                 std::uint64_t seed = 0x5454795041434bULL) -> std::string {
    std::string output{input};
    if (output.empty()) return random_sequence(1, seed);
    const auto position = static_cast<std::size_t>(detail::next(seed) % output.size());
    output[position] = static_cast<char>(detail::next(seed) & 0x7fU);
    return output;
}

inline void stress_test(Automaton& automaton, const std::size_t iterations,
                        std::uint64_t seed = 0x5454795041434bULL) {
    for (std::size_t index = 0; index < iterations; ++index) {
        automaton.feed(random_sequence(static_cast<std::size_t>(detail::next(seed) % 96U), seed));
    }
}

}  // namespace ttytk::fuzz

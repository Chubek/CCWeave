#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-CellBuffer.hpp>
#include <TTyTk/TTyTk-ColorTools.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace ttytk::prompt {

struct Segment {
    std::string text{};
    color::Rgb foreground{255, 255, 255};
    color::Rgb background{};
    std::string icon{};
};

using SegmentList = std::vector<Segment>;

// NOTE(agent): The manifest leaves Segment construction and render's return
// type unspecified; a value-owned segment list and UTF-8 ANSI sequence are
// the smallest useful public representation.
[[nodiscard]] inline auto render(const SegmentList& segments, const std::size_t columns = 0)
    -> std::string {
    std::string output;
    std::size_t used = 0;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto& segment = segments[index];
        const auto visible = segment.icon.size() + segment.text.size() +
                             (index + 1 < segments.size() ? std::size_t{1} : 0);
        if (columns != 0 && used + visible > columns) break;

        output += "\x1b[38;2;" + std::to_string(segment.foreground.red) + ';' +
                  std::to_string(segment.foreground.green) + ';' +
                  std::to_string(segment.foreground.blue) + ";48;2;" +
                  std::to_string(segment.background.red) + ';' +
                  std::to_string(segment.background.green) + ';' +
                  std::to_string(segment.background.blue) + 'm';
        output += segment.icon + segment.text;
        used += segment.icon.size() + segment.text.size();

        if (index + 1 < segments.size()) {
            const auto& next = segments[index + 1];
            output += "\x1b[38;2;" + std::to_string(segment.background.red) + ';' +
                      std::to_string(segment.background.green) + ';' +
                      std::to_string(segment.background.blue) + ";48;2;" +
                      std::to_string(next.background.red) + ';' +
                      std::to_string(next.background.green) + ';' +
                      std::to_string(next.background.blue) + "m\xee\x82\xb0";
            ++used;
        }
    }
    return output + "\x1b[0m";
}

}  // namespace ttytk::prompt

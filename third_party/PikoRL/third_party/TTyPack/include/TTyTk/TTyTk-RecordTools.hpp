#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-Automaton.hpp>

#include <chrono>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttytk {

class Recorder {
public:
    inline void start() {
        events_.clear();
        started_ = std::chrono::steady_clock::now();
        recording_ = true;
    }

    inline void stop() noexcept { recording_ = false; }

    inline void record_output(const std::string_view bytes) {
        if (!recording_) return;
        events_.emplace_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count(),
                             std::string{bytes});
    }

    [[nodiscard]] inline auto save_asciicast(const std::string& path) const -> Result<void> {
        std::ofstream file{path};
        if (!file) return Error{1, "unable to open asciicast output"};
        file << "{\"version\":2,\"width\":80,\"height\":24,\"timestamp\":0,\"env\":{}}\n";
        for (const auto& [time, bytes] : events_) {
            file << '[' << time << ",\"o\",";
            write_json(file, bytes);
            file << "]\n";
        }
        if (!file) return Error{1, "unable to write asciicast output"};
        return {};
    }

private:
    static inline void write_json(std::ofstream& file, const std::string_view value) {
        file.put('"');
        for (const char character : value) {
            if (character == '"' || character == '\\') file.put('\\');
            if (character == '\n') file << "\\n";
            else if (character == '\r') file << "\\r";
            else file.put(character);
        }
        file.put('"');
    }

    bool recording_{};
    std::chrono::steady_clock::time_point started_{};
    std::vector<std::pair<double, std::string>> events_{};
};

}  // namespace ttytk

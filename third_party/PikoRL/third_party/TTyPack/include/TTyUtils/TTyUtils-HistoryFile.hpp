#pragma once

#include <TTyUtils/TTyUtils-Config.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::hist {

struct Entry {
    std::string text{};
    std::string timestamp{};
    std::string cwd{};
    int status{};
};

enum class Merge { all, adjacent, none };

[[nodiscard]] inline auto dedup(std::vector<Entry> entries, const Merge policy = Merge::adjacent) -> std::vector<Entry> {
    if (policy == Merge::none) return entries;
    std::vector<Entry> result;
    for (auto& entry : entries) {
        const auto duplicate = policy == Merge::adjacent
                                   ? (!result.empty() && result.back().text == entry.text)
                                   : std::any_of(result.begin(), result.end(), [&entry](const Entry& prior) {
                                         return prior.text == entry.text;
                                     });
        if (!duplicate) result.push_back(std::move(entry));
    }
    return result;
}

class History {
public:
    explicit History(std::filesystem::path path, const Merge merge = Merge::adjacent)
        : path_(std::move(path)), merge_(merge) {}

    [[nodiscard]] auto entries() const noexcept -> const std::vector<Entry>& { return entries_; }

    // NOTE(agent): Config is the sole declared dependency, so this header
    // cannot name TTyTk's Result until Phase 9 supplies Config's Core include.
    // The conservative file API reports success with bool.
    [[nodiscard]] auto load() -> bool {
        entries_.clear();
        std::ifstream input(path_);
        if (!input) {
            return !std::filesystem::exists(path_);
        }
        std::string line;
        while (std::getline(input, line)) {
            Entry entry;
            const auto one = line.find('\t');
            const auto two = one == std::string::npos ? std::string::npos : line.find('\t', one + 1);
            const auto three = two == std::string::npos ? std::string::npos : line.find('\t', two + 1);
            if (three == std::string::npos) {
                entry.text = line;
            } else {
                entry.timestamp = line.substr(0, one);
                entry.cwd = line.substr(one + 1, two - one - 1);
                entry.status = std::atoi(line.substr(two + 1, three - two - 1).c_str());
                entry.text = line.substr(three + 1);
            }
            entries_.push_back(std::move(entry));
        }
        if (!input.eof()) return false;
        entries_ = dedup(std::move(entries_), merge_);
        return true;
    }

    [[nodiscard]] auto append(Entry entry) -> bool {
        if (entry.text.empty() || entry.text.starts_with(' ')) return true;
        if (entry.timestamp.empty()) {
            entry.timestamp = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        }
        std::ofstream output(path_, std::ios::app);
        if (!output) return false;
        output << entry.timestamp << '\t' << entry.cwd << '\t' << entry.status << '\t' << entry.text << '\n';
        if (!output) return false;
        entries_.push_back(std::move(entry));
        entries_ = dedup(std::move(entries_), merge_);
        return true;
    }

    [[nodiscard]] auto match_prefix(const std::string_view prefix) const -> std::vector<Entry> {
        std::vector<Entry> result;
        for (const auto& entry : entries_) {
            if (entry.text.starts_with(prefix)) result.push_back(entry);
        }
        return result;
    }

private:
    std::filesystem::path path_;
    Merge merge_;
    std::vector<Entry> entries_;
};

}  // namespace ttyutils::hist

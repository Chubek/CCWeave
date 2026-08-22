#pragma once

#include <TTyUtils/TTyUtils-Search.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::completion {

// NOTE(agent): `compl` is a C++ alternative operator token for `~` and cannot
// be used as a namespace identifier. The manifest's `compl::` spelling is
// therefore represented by the valid `completion::` namespace.
struct Candidate {
    std::string text{};
    std::string display{};
    int score{};
};

[[nodiscard]] inline auto rank(const std::string_view query, const std::string_view candidate) -> int {
    if (candidate.starts_with(query)) return 1000 - static_cast<int>(candidate.size());
    int score = 0;
    std::size_t at = 0;
    for (const auto character : query) {
        const auto found = candidate.find(character, at);
        if (found == std::string_view::npos) return -1;
        score += found == at ? 10 : 1;
        at = found + 1;
    }
    return score;
}

class Source {
public:
    virtual ~Source() = default;
    [[nodiscard]] virtual auto complete(std::string_view query) -> std::vector<Candidate> = 0;
};

class Engine {
public:
    void add(std::unique_ptr<Source> source) { sources_.push_back(std::move(source)); }

    [[nodiscard]] auto query(const std::string_view text, const std::size_t page = 0,
                             const std::size_t page_size = 64) -> std::vector<Candidate> {
        std::vector<Candidate> result;
        for (const auto& source : sources_) {
            auto candidates = source->complete(text);
            result.insert(result.end(), std::make_move_iterator(candidates.begin()),
                          std::make_move_iterator(candidates.end()));
        }
        for (auto& candidate : result) {
            candidate.score = std::max(candidate.score, rank(text, candidate.text));
            if (candidate.display.empty()) candidate.display = candidate.text;
        }
        std::sort(result.begin(), result.end(), [](const Candidate& left, const Candidate& right) {
            return left.score > right.score;
        });
        const auto begin = std::min(page * page_size, result.size());
        const auto end = std::min(begin + page_size, result.size());
        return {result.begin() + static_cast<std::ptrdiff_t>(begin),
                result.begin() + static_cast<std::ptrdiff_t>(end)};
    }

private:
    std::vector<std::unique_ptr<Source>> sources_;
};

class FilenameSource final : public Source {
public:
    explicit FilenameSource(std::filesystem::path root = ".") : root_(std::move(root)) {}
    [[nodiscard]] auto complete(const std::string_view query) -> std::vector<Candidate> override {
        std::vector<Candidate> result;
        const std::filesystem::path prefix{query};
        const auto parent = prefix.has_parent_path() ? prefix.parent_path() : root_;
        const auto stem = prefix.filename().string();
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(parent, error)) {
            if (error) break;
            const auto name = entry.path().filename().string();
            if (name.starts_with(stem)) result.push_back({entry.path().string(), name, 0});
        }
        return result;
    }

private:
    std::filesystem::path root_;
};

class WordSource final : public Source {
public:
    explicit WordSource(std::vector<std::string> words) : words_(std::move(words)) {}
    [[nodiscard]] auto complete(const std::string_view query) -> std::vector<Candidate> override {
        std::vector<Candidate> result;
        for (const auto& word : words_) if (rank(query, word) >= 0) result.push_back({word, word, 0});
        return result;
    }

private:
    std::vector<std::string> words_;
};

}  // namespace ttyutils::completion

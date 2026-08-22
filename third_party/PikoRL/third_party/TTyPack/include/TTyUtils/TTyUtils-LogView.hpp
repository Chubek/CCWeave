#pragma once

#include <TTyUtils/TTyUtils-EventLoop.hpp>
#include <TTyUtils/TTyUtils-Pager.hpp>
#include <TTyUtils/TTyUtils-Search.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace ttyutils::logview {

struct Rule {
    std::string pattern{};
    theme::Style style{};
};

class Filter {
public:
    enum class Mode { Include, Exclude };
    Filter() = default;
    Filter(Mode mode, std::string pattern) : mode_(mode), pattern_(std::move(pattern)) {}
    [[nodiscard]] static auto include(std::string pattern) -> Filter { return {Mode::Include, std::move(pattern)}; }
    [[nodiscard]] static auto exclude(std::string pattern) -> Filter { return {Mode::Exclude, std::move(pattern)}; }
    [[nodiscard]] auto accepts(const std::string_view text) const -> bool {
        const auto found = pattern_.empty() || text.find(pattern_) != std::string_view::npos;
        return mode_ == Mode::Include ? found : !found;
    }

private:
    Mode mode_{Mode::Include};
    std::string pattern_;
};

inline auto tail_file(const std::filesystem::path& path) -> pager::Source {
    std::ifstream input(path);
    return pager::Source{std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()}};
}

class LogView : public pager::Pager {
public:
    explicit LogView(pager::Source source = {}) : pager::Pager(std::move(source)) {}
    void add_rule(Rule rule) { rules_.push_back(std::move(rule)); }
    void set_filter(Filter filter) { filter_ = std::move(filter); }

private:
    std::vector<Rule> rules_;
    Filter filter_;
};

}  // namespace ttyutils::logview

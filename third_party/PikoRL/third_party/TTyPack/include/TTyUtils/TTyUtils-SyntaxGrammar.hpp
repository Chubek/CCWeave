#pragma once

#include <TTyUtils/TTyUtils-Config.hpp>
#include <TTyUtils/TTyUtils-Search.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ttyutils::syntax {

class Scope {
public:
    Scope() = default;
    explicit Scope(std::string name) : name_(std::move(name)) {}
    [[nodiscard]] auto name() const noexcept -> std::string_view { return name_; }
    [[nodiscard]] friend auto operator==(const Scope&, const Scope&) -> bool = default;

private:
    std::string name_;
};

class Grammar {
public:
    std::string id{};
    std::string name{};
    std::vector<std::string> filename_globs{};
    std::vector<std::string> keywords{};
    Scope root_scope{};

    [[nodiscard]] auto matches_filename(const std::string_view filename) const -> bool {
        for (const auto& glob : filename_globs) {
            if (glob.starts_with("*.") && filename.ends_with(std::string_view(glob).substr(1))) return true;
            if (glob == filename) return true;
        }
        return false;
    }
};

namespace detail {

[[nodiscard]] inline auto field(const std::string_view input, const std::string_view name) -> std::string {
    const auto marker = "\"" + std::string(name) + "\"";
    const auto key = input.find(marker);
    if (key == std::string_view::npos) return {};
    const auto colon = input.find(':', key + marker.size());
    const auto quote = colon == std::string_view::npos ? std::string_view::npos : input.find('"', colon + 1);
    const auto end = quote == std::string_view::npos ? std::string_view::npos : input.find('"', quote + 1);
    return end == std::string_view::npos ? std::string{} : std::string(input.substr(quote + 1, end - quote - 1));
}

[[nodiscard]] inline auto quoted_after(const std::string_view input, const std::string_view name) -> std::vector<std::string> {
    std::vector<std::string> output;
    const auto marker = "\"" + std::string(name) + "\"";
    const auto key = input.find(marker);
    const auto open = key == std::string_view::npos ? std::string_view::npos : input.find('[', key + marker.size());
    const auto close = open == std::string_view::npos ? std::string_view::npos : input.find(']', open + 1);
    if (close == std::string_view::npos) return output;
    for (std::size_t at = open + 1; at < close;) {
        const auto first = input.find('"', at);
        if (first == std::string_view::npos || first >= close) break;
        const auto last = input.find('"', first + 1);
        if (last == std::string_view::npos || last >= close) break;
        output.emplace_back(input.substr(first + 1, last - first - 1));
        at = last + 1;
    }
    return output;
}

}  // namespace detail

// NOTE(agent): TextMate grammars are JSON and usually regex-rich. Without a
// JSON dependency, load_tm conservatively loads identity, fileTypes, and
// keyword data used by the Phase 6 lexical highlighter.
[[nodiscard]] inline auto load_tm(const std::filesystem::path& path) -> ttytk::Result<Grammar> {
    std::ifstream input(path);
    if (!input) return ttytk::Error{1, "unable to open grammar"};
    std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    Grammar grammar;
    grammar.id = detail::field(source, "scopeName");
    grammar.name = detail::field(source, "name");
    grammar.filename_globs = detail::quoted_after(source, "fileTypes");
    for (auto& type : grammar.filename_globs) {
        if (!type.starts_with('*')) type.insert(0, "*.");
    }
    grammar.keywords = detail::quoted_after(source, "keywords");
    grammar.root_scope = Scope{grammar.id.empty() ? "source.plain" : grammar.id};
    if (grammar.id.empty()) return ttytk::Error{2, "grammar has no scopeName"};
    return grammar;
}

class Registry {
public:
    [[nodiscard]] static auto global() -> Registry& {
        static Registry registry;
        return registry;
    }

    void add(Grammar grammar) { grammars_.push_back(std::move(grammar)); }

    void load_dir(const std::filesystem::path& directory) {
        std::error_code error;
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (error || !entry.is_regular_file()) break;
            const auto extension = entry.path().extension();
            if (extension != ".json" && extension != ".tmLanguage") continue;
            if (auto grammar = load_tm(entry.path())) add(std::move(grammar).value());
        }
    }

    [[nodiscard]] auto by_filename(const std::string_view filename) const -> const Grammar* {
        for (const auto& grammar : grammars_) {
            if (grammar.matches_filename(filename)) return &grammar;
        }
        return nullptr;
    }

    [[nodiscard]] auto by_id(const std::string_view id) const -> const Grammar* {
        for (const auto& grammar : grammars_) if (grammar.id == id) return &grammar;
        return nullptr;
    }

private:
    std::vector<Grammar> grammars_;
};

[[nodiscard]] inline auto detect(const Registry& registry, const std::string_view filename,
                                 const std::string_view first_line = {}) -> const Grammar* {
    if (const auto* grammar = registry.by_filename(filename)) return grammar;
    if (first_line.starts_with("#!")) {
        for (const auto id : {"python", "sh", "bash", "ruby"}) {
            if (first_line.find(id) != std::string_view::npos) {
                if (const auto* grammar = registry.by_id(id)) return grammar;
            }
        }
    }
    return nullptr;
}

}  // namespace ttyutils::syntax

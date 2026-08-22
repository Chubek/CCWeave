#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-EscapeCodes.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>

namespace ttytk::terminfo {

struct Entry {
    std::string names{};
    std::vector<bool> flags{};
    std::vector<int> numbers{};
    std::vector<std::optional<std::string>> strings{};

    [[nodiscard]] inline auto flag(const std::size_t index) const -> bool {
        return index < flags.size() && flags[index];
    }
    [[nodiscard]] inline auto num(const std::size_t index) const -> std::optional<int> {
        if (index >= numbers.size() || numbers[index] < 0) return std::nullopt;
        return numbers[index];
    }
    [[nodiscard]] inline auto str(const std::size_t index) const
        -> std::optional<std::string_view> {
        if (index >= strings.size() || !strings[index]) return std::nullopt;
        return *strings[index];
    }
};

namespace detail {
inline auto word(const std::string_view data, const std::size_t offset) -> std::optional<std::uint16_t> {
    if (offset + 2 > data.size()) return std::nullopt;
    return static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset])) |
           (static_cast<std::uint16_t>(static_cast<unsigned char>(data[offset + 1])) << 8U);
}
inline auto signed_word(const std::string_view data, const std::size_t offset) -> std::optional<int> {
    auto value = word(data, offset); if (!value) return std::nullopt;
    return static_cast<std::int16_t>(*value);
}
}  // namespace detail

// Terminfo.man: compiled descriptions use a six-short header and NUL-terminated strings.
[[nodiscard]] inline auto load_from(const std::string_view data) -> Result<Entry> {
    auto magic = detail::word(data, 0); auto names_size = detail::word(data, 2);
    auto bool_count = detail::word(data, 4); auto number_count = detail::word(data, 6);
    auto string_count = detail::word(data, 8); auto table_size = detail::word(data, 10);
    if (!magic || !names_size || !bool_count || !number_count || !string_count || !table_size ||
        (*magic != 0432 && *magic != 01036)) return Error{EINVAL, "invalid terminfo header"};
    std::size_t at = 12;
    if (at + *names_size > data.size()) return Error{EINVAL, "truncated terminfo names"};
    Entry entry{}; entry.names.assign(data.substr(at, *names_size).data());
    at += *names_size;
    if (at + *bool_count > data.size()) return Error{EINVAL, "truncated terminfo flags"};
    entry.flags.reserve(*bool_count);
    for (std::size_t i = 0; i < *bool_count; ++i) entry.flags.push_back(data[at + i] != 0);
    at += *bool_count; if ((at & 1U) != 0U) ++at;
    entry.numbers.reserve(*number_count);
    for (std::size_t i = 0; i < *number_count; ++i) {
        auto value = detail::signed_word(data, at + i * 2); if (!value) return Error{EINVAL, "truncated terminfo numbers"};
        entry.numbers.push_back(*value);
    }
    at += static_cast<std::size_t>(*number_count) * 2;
    std::vector<int> offsets{}; offsets.reserve(*string_count);
    for (std::size_t i = 0; i < *string_count; ++i) {
        auto value = detail::signed_word(data, at + i * 2); if (!value) return Error{EINVAL, "truncated terminfo offsets"};
        offsets.push_back(*value);
    }
    at += static_cast<std::size_t>(*string_count) * 2;
    if (at + *table_size > data.size()) return Error{EINVAL, "truncated terminfo string table"};
    const auto table = data.substr(at, *table_size); entry.strings.resize(*string_count);
    for (std::size_t i = 0; i < offsets.size(); ++i) if (offsets[i] >= 0 && static_cast<std::size_t>(offsets[i]) < table.size()) {
        const auto start = static_cast<std::size_t>(offsets[i]); const auto end = table.find('\0', start);
        if (end == std::string_view::npos) return Error{EINVAL, "unterminated terminfo string"};
        entry.strings[i] = std::string(table.substr(start, end - start));
    }
    return entry;
}

[[nodiscard]] inline auto search_paths() -> std::vector<std::string> {
    std::vector<std::string> paths{};
    if (const char* terminfo = std::getenv("TERMINFO"); terminfo != nullptr) paths.emplace_back(terminfo);
    if (const char* home = std::getenv("HOME"); home != nullptr) paths.emplace_back(std::string(home) + "/.terminfo");
    if (const char* dirs = std::getenv("TERMINFO_DIRS"); dirs != nullptr) {
        std::string value(dirs); std::size_t begin = 0;
        while (begin <= value.size()) { const auto end = value.find(':', begin); paths.push_back(value.substr(begin, end - begin)); if (end == std::string::npos) break; begin = end + 1; }
    }
    paths.emplace_back("/usr/share/terminfo"); return paths;
}
[[nodiscard]] inline auto load(const std::string& name) -> Result<Entry> {
    if (name.empty()) return Error{EINVAL, "empty terminal name"};
    for (const auto& path : search_paths()) {
        const auto file = std::filesystem::path(path) / std::string(1, name.front()) / name;
        std::ifstream stream(file, std::ios::binary); if (!stream) continue;
        const std::string data((std::istreambuf_iterator<char>(stream)), {});
        return load_from(data);
    }
    return Error{ENOENT, "terminfo entry not found"};
}
// Terminfo.man §Parameterized Strings: minimal stack evaluator for common %pN/%d sequences.
[[nodiscard]] inline auto tparm(const std::string_view pattern, std::vector<int> parameters) -> std::string {
    std::string out{}; std::vector<int> stack{};
    for (std::size_t i = 0; i < pattern.size(); ++i) { if (pattern[i] != '%' || ++i == pattern.size()) { out += pattern[i]; continue; }
        const char op = pattern[i]; if (op == 'i') { if (parameters.size()>0) ++parameters[0]; if (parameters.size()>1) ++parameters[1]; }
        else if (op == 'p' && i+1<pattern.size() && pattern[i+1]>='1' && pattern[i+1]<='9') stack.push_back(parameters.size()>static_cast<std::size_t>(pattern[++i]-'1') ? parameters[pattern[i]-'1'] : 0);
        else if (op == 'd' && !stack.empty()) { out += std::to_string(stack.back()); stack.pop_back(); }
        else if (op == 'c' && !stack.empty()) { out += static_cast<char>(stack.back()); stack.pop_back(); }
        else if (op == '%' ) out += '%';
    } return out;
}
}  // namespace ttytk::terminfo

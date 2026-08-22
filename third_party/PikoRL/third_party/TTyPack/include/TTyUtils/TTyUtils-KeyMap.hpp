#pragma once

#include <TTyTk/TTyTk-InputProto.hpp>
#include <TTyUtils/TTyUtils-Config.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ttyutils::keymap {

struct Chord {
    std::vector<std::string> keys{};
};

using Action = std::string;

[[nodiscard]] inline auto parse_key(const std::string_view notation) -> Chord {
    Chord chord;
    std::size_t start = 0;
    while (start <= notation.size()) {
        const auto end = notation.find(' ', start);
        chord.keys.emplace_back(notation.substr(start, end == std::string_view::npos ? notation.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return chord;
}

class WhichKey {
public:
    void set(std::vector<std::string> hints) { hints_ = std::move(hints); }
    [[nodiscard]] auto hints() const noexcept -> const std::vector<std::string>& { return hints_; }

private:
    std::vector<std::string> hints_;
};

class Map {
public:
    void bind(const std::string_view notation, Action action) { bindings_[canonical(parse_key(notation))] = std::move(action); }
    [[nodiscard]] auto lookup(const std::string_view notation) const -> std::optional<Action> {
        const auto found = bindings_.find(canonical(parse_key(notation)));
        return found == bindings_.end() ? std::nullopt : std::optional{found->second};
    }
    void on_pending(std::function<void(std::vector<std::string>)> callback) { pending_ = std::move(callback); }

private:
    [[nodiscard]] static auto canonical(const Chord& chord) -> std::string {
        std::string result;
        for (const auto& key : chord.keys) result += key + " ";
        return result;
    }
    std::unordered_map<std::string, Action> bindings_;
    std::function<void(std::vector<std::string>)> pending_{};
};

}  // namespace ttyutils::keymap

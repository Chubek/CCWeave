#pragma once

#include <TTyTk/TTyTk-Core.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ttyutils::config {

[[nodiscard]] inline auto xdg_path(std::string_view kind) -> std::filesystem::path;

class Value {
public:
    using Storage = std::variant<std::monostate, bool, long long, double, std::string>;
    Value() = default;
    Value(bool value) : value_(value) {}
    Value(long long value) : value_(value) {}
    Value(double value) : value_(value) {}
    Value(std::string value) : value_(std::move(value)) {}

    template <typename T>
    [[nodiscard]] auto as(const T& fallback = T{}) const -> T {
        if (const auto* value = std::get_if<T>(&value_)) return *value;
        return fallback;
    }
    [[nodiscard]] auto empty() const noexcept -> bool { return std::holds_alternative<std::monostate>(value_); }

private:
    Storage value_;
};

struct Schema {
    std::vector<std::string> required{};
    [[nodiscard]] auto valid(const std::unordered_map<std::string, Value>& values) const -> bool {
        for (const auto& key : required) if (!values.contains(key)) return false;
        return true;
    }
};

class Store {
public:
    [[nodiscard]] static auto load(const std::string& name) -> Store {
        Store result;
        const auto path = xdg_path("config") / (name + ".toml");
        std::ifstream input(path);
        if (input) result.parse(input);
        return result;
    }
    [[nodiscard]] auto get_value(const std::string_view key) const -> Value {
        const auto found = values_.find(std::string(key));
        return found == values_.end() ? Value{} : found->second;
    }
    template <typename T>
    [[nodiscard]] auto get(const std::string_view key, const T& fallback = T{}) const -> T {
        return get_value(key).as<T>(fallback);
    }
    void set(std::string key, Value value) { values_[std::move(key)] = std::move(value); }
    [[nodiscard]] auto values() const noexcept -> const std::unordered_map<std::string, Value>& { return values_; }

private:
    void parse(std::istream& input) {
        std::string line;
        while (std::getline(input, line)) {
            const auto equal = line.find('=');
            if (equal == std::string::npos) continue;
            auto key = line.substr(0, equal);
            auto value = line.substr(equal + 1);
            while (!key.empty() && key.back() == ' ') key.pop_back();
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            if (value == "true" || value == "false") set(std::move(key), Value{value == "true"});
            else if (!value.empty() && value.front() == '"') set(std::move(key), Value{value.substr(1, value.size() - 2)});
            else set(std::move(key), Value{std::atoll(value.c_str())});
        }
    }
    std::unordered_map<std::string, Value> values_;
};

inline auto watch(Store&, std::function<void()> callback) -> void { callback(); }

inline auto xdg_path(const std::string_view kind) -> std::filesystem::path {
    const char* base = kind == "config" ? std::getenv("XDG_CONFIG_HOME") : std::getenv("XDG_DATA_HOME");
    if (base != nullptr) return base;
    const char* home = std::getenv("HOME");
    return std::filesystem::path{home == nullptr ? "." : home} /
           (kind == "config" ? ".config" : ".local/share");
}

}  // namespace ttyutils::config

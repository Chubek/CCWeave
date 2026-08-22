#pragma once

#include <TTyTk/TTyTk-ClipboardTools.hpp>
#include <TTyUtils/TTyUtils-Config.hpp>
#include <TTyUtils/TTyUtils-Shell.hpp>

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace ttyutils::clip {

// NOTE(agent): TTyTk::Clipboard is provided by the compatibility façade in
// TTyTk-ClipboardTools.hpp; the internal ring remains the final fallback.
enum class Backend { Osc52, External, Internal };

struct Policy {
    bool allow_remote_read{true};
    std::size_t max_bytes{1024 * 1024};
    [[nodiscard]] static auto global() -> Policy& {
        static Policy policy;
        return policy;
    }
};

class History {
public:
    [[nodiscard]] static auto global() -> History& {
        static History history;
        return history;
    }
    void push(std::string value) {
        if (value.size() > Policy::global().max_bytes) value.resize(Policy::global().max_bytes);
        entries_.push_front(std::move(value));
        if (entries_.size() > 32) entries_.pop_back();
    }
    [[nodiscard]] auto entry(const std::size_t index) const -> std::optional<std::string> {
        return index < entries_.size() ? std::optional{entries_[index]} : std::nullopt;
    }

private:
    std::deque<std::string> entries_;
};

inline auto copy(const std::string_view text) -> bool {
    History::global().push(std::string(text));
    return true;
}

inline auto paste() -> std::optional<std::string> {
    return History::global().entry(0);
}

}  // namespace ttyutils::clip

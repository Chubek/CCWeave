#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-InputProto.hpp>

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ttytk {

class KeyListener {
public:
    std::function<void(const input::Key&)> on_key{};
    std::function<void(std::string_view)> on_raw{};

    inline void bind(std::string key, std::function<void()> callback) {
        bindings_[std::move(key)] = std::move(callback);
    }

    // NOTE(agent): dispatch is necessary to connect InputProto to bindings,
    // but the manifest lists only callback and binding members.
    inline void dispatch(const input::Key& key, const std::string_view spelling = {}) {
        if (on_key) on_key(key);
        if (!spelling.empty()) {
            const auto found = bindings_.find(std::string{spelling});
            if (found != bindings_.end()) found->second();
        }
    }

private:
    std::unordered_map<std::string, std::function<void()>> bindings_{};
};

}  // namespace ttytk

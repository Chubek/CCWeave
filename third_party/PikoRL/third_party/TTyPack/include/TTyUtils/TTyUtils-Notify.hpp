#pragma once

#include <TTyUtils/TTyUtils-Config.hpp>
#include <TTyUtils/TTyUtils-Shell.hpp>

#include <cstdio>
#include <string>
#include <string_view>

namespace ttyutils::notify {

// NOTE(agent): External notify-send/osascript backends require the later Shell
// integration; send() reports whether a message is non-empty.
enum class Urgency { Low, Normal, Critical };
enum class Backend { Terminal, External, Bell };

struct Message {
    std::string title{};
    std::string body{};
    Urgency urgency{Urgency::Normal};
};

inline auto send(const Message& message) -> bool {
    return !message.title.empty() || !message.body.empty();
}

inline void bell() {
    (void)std::fwrite("\a", 1, 1, stdout);
    (void)std::fflush(stdout);
}

}  // namespace ttyutils::notify

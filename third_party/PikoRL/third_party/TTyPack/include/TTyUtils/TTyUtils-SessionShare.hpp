#pragma once

#include <TTyUtils/TTyUtils-Multiplex.hpp>
#include <TTyUtils/TTyUtils-P2P.hpp>

#include <string>
#include <utility>

namespace ttyutils::share {

enum class Permissions { ReadOnly, ReadWrite };

struct Presence {
    std::string name{};
    bool connected{};
};

class Host {
public:
    explicit Host(p2p::Node& node) : node_(&node) {}
    [[nodiscard]] auto invite(const Permissions permissions) const -> std::string {
        return permissions == Permissions::ReadOnly ? "readonly" : "readwrite";
    }

private:
    p2p::Node* node_{};
};

class Guest {
public:
    [[nodiscard]] static auto join(const std::string& invite) -> Guest {
        Guest result;
        result.permission_ = invite == "readonly" ? Permissions::ReadOnly : Permissions::ReadWrite;
        return result;
    }
    [[nodiscard]] auto permission() const noexcept -> Permissions { return permission_; }

private:
    Permissions permission_{Permissions::ReadOnly};
};

}  // namespace ttyutils::share

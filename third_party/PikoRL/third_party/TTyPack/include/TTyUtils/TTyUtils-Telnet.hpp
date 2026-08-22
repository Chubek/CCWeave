#pragma once

#include <TTyUtils/TTyUtils-NetLink.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ttyutils::telnet {

enum class Option : std::uint8_t { Echo = 1, SuppressGoAhead = 3, Binary = 0, Naws = 31, Ttype = 24 };

class Negotiator {
public:
    [[nodiscard]] static auto escape(const std::string_view data) -> std::string {
        std::string result;
        for (const auto value : data) {
            result.push_back(value);
            if (static_cast<unsigned char>(value) == 255) result.push_back(value);
        }
        return result;
    }
};

class Session {
public:
    explicit Session(net::Link link = {}) : link_(std::move(link)) {}
    void enable(const Option option) { enabled_ |= (1ULL << static_cast<unsigned>(option)); }
    [[nodiscard]] auto enabled(const Option option) const noexcept -> bool {
        return (enabled_ & (1ULL << static_cast<unsigned>(option))) != 0;
    }
    [[nodiscard]] auto send(const std::string_view data) -> bool { return link_.write(Negotiator::escape(data)); }
    [[nodiscard]] auto send_window_size(const std::uint16_t columns, const std::uint16_t rows) -> bool {
        const std::string payload = "\xff\xfa\x1f" + std::string{static_cast<char>(columns >> 8U),
            static_cast<char>(columns), static_cast<char>(rows >> 8U), static_cast<char>(rows)} + "\xff\xf0";
        return link_.write(payload);
    }
    [[nodiscard]] auto link() noexcept -> net::Link& { return link_; }

private:
    net::Link link_;
    std::uint64_t enabled_{};
};

}  // namespace ttyutils::telnet

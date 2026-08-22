#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__linux__)
#define TTYTK_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#define TTYTK_PLATFORM_MACOS 1
#else
#error "not implemented on this platform"
#endif

namespace ttytk {

// NOTE(agent): The manifest does not specify version components or an exposed
// version structure, so version() conservatively returns {major, minor, patch}.
[[nodiscard]] constexpr auto version() noexcept -> std::array<unsigned, 3> {
    return {0U, 1U, 0U};
}

struct Error {
    int code{};
    std::string message{};

    [[nodiscard]] friend auto operator==(const Error&, const Error&) -> bool =
        default;
};

// NOTE(agent): std::expected is unavailable in the required C++20 standard.
// This minimal value-or-error type supplies the manifest's Result abstraction.
template <typename T>
class Result {
public:
    Result(const T& value) : storage_(value) {}
    Result(T&& value) : storage_(std::move(value)) {}
    Result(const Error& error) : storage_(error) {}
    Result(Error&& error) : storage_(std::move(error)) {}

    [[nodiscard]] auto has_value() const noexcept -> bool {
        return std::holds_alternative<T>(storage_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] auto value() & -> T& {
        return std::get<T>(storage_);
    }

    [[nodiscard]] auto value() const& -> const T& {
        return std::get<T>(storage_);
    }

    [[nodiscard]] auto value() && -> T&& {
        return std::get<T>(std::move(storage_));
    }

    [[nodiscard]] auto error() & -> Error& {
        return std::get<Error>(storage_);
    }

    [[nodiscard]] auto error() const& -> const Error& {
        return std::get<Error>(storage_);
    }

private:
    std::variant<T, Error> storage_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(const Error& error) : error_(error) {}
    Result(Error&& error) : error_(std::move(error)) {}

    [[nodiscard]] auto has_value() const noexcept -> bool {
        return !error_.has_value();
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] auto error() & -> Error& {
        return *error_;
    }

    [[nodiscard]] auto error() const& -> const Error& {
        return *error_;
    }

private:
    std::optional<Error> error_;
};

template <typename T>
using Span = std::span<T>;

using Bytes = std::vector<std::byte>;

}  // namespace ttytk

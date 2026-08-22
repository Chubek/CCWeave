#pragma once

#include <TTyUtils/TTyUtils-Theme.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::ui {

enum class Align { Left, Center, Right };

struct Segment {
    std::string text{};
    theme::Style style{};
    int priority{};
    std::function<void()> action{};
};

class StatusBar : public Widget {
public:
    void left(std::vector<Segment> segments) { left_ = std::move(segments); }
    void center(std::vector<Segment> segments) { center_ = std::move(segments); }
    void right(std::vector<Segment> segments) { right_ = std::move(segments); }
    void flash(std::string message, const std::chrono::milliseconds duration = std::chrono::seconds{2}) {
        transient_ = std::move(message);
        transient_until_ = std::chrono::steady_clock::now() + duration;
    }
    [[nodiscard]] auto message() const -> std::string {
        return std::chrono::steady_clock::now() < transient_until_ ? transient_ : std::string{};
    }
    void paint(Canvas& canvas) override {
        std::string line = message();
        for (const auto& segment : left_) line += segment.text;
        for (const auto& segment : center_) line += segment.text;
        for (const auto& segment : right_) line += segment.text;
        canvas.text(rect().x, rect().y, line, style());
    }

private:
    std::vector<Segment> left_;
    std::vector<Segment> center_;
    std::vector<Segment> right_;
    std::string transient_;
    std::chrono::steady_clock::time_point transient_until_{};
};

inline auto transient(std::string text, const std::chrono::milliseconds duration = std::chrono::seconds{2}) -> Segment {
    (void)duration;
    return Segment{std::move(text)};
}

}  // namespace ttyutils::ui

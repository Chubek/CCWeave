#pragma once

#include <TTyUtils/TTyUtils-EventLoop.hpp>
#include <TTyUtils/TTyUtils-Widget.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ttyutils::ui {

class EtaModel {
public:
    void update(const double amount, const std::chrono::steady_clock::duration elapsed) {
        if (elapsed.count() > 0) rate_ = amount / std::chrono::duration<double>(elapsed).count();
    }
    [[nodiscard]] auto rate() const noexcept -> double { return rate_; }
    [[nodiscard]] auto remaining(const double amount) const noexcept -> double {
        return rate_ <= 0.0 ? 0.0 : amount / rate_;
    }

private:
    double rate_{};
};

class ProgressBar : public Widget {
public:
    void set_total(const double total) noexcept { total_ = std::max(0.0, total); }
    void set_value(const double value) noexcept { value_ = std::clamp(value, 0.0, total_); }
    [[nodiscard]] auto fraction() const noexcept -> double { return total_ == 0.0 ? 0.0 : value_ / total_; }
    void advance(const double amount) noexcept { set_value(value_ + amount); }
    void paint(Canvas& canvas) override {
        const auto width = std::max(0, rect().width);
        const auto filled = static_cast<int>(fraction() * width);
        canvas.text(rect().x, rect().y, std::string(static_cast<std::size_t>(filled), '=') +
                    std::string(static_cast<std::size_t>(width - filled), ' '), style());
    }

private:
    double total_{};
    double value_{};
};

class Spinner : public Widget {
public:
    void tick() noexcept { frame_ = (frame_ + 1) % 4; }
    void paint(Canvas& canvas) override { canvas.text(rect().x, rect().y, std::string(1, "|/-\\"[frame_]), style()); }

private:
    std::size_t frame_{};
};

class MultiProgress {
public:
    class Task {
    public:
        void advance(const double amount) noexcept { if (value_ != nullptr) *value_ += amount; }
        [[nodiscard]] auto value() const noexcept -> double { return value_ == nullptr ? 0.0 : *value_; }

    private:
        friend class MultiProgress;
        explicit Task(double& value) : value_(&value) {}
        double* value_{};
    };

    Task add(std::string name, const double total) {
        entries_.push_back({std::move(name), total, 0.0});
        return Task{entries_.back().value};
    }

private:
    struct Entry { std::string name; double total; double value; };
    std::vector<Entry> entries_;
};

}  // namespace ttyutils::ui

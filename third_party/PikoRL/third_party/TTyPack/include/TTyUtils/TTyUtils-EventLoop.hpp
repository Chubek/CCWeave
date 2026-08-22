#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-InputProto.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#else
#error "not implemented on this platform"
#endif

namespace ttyutils::loop {

// NOTE(agent): The manifest names TTyTk::Input; this repository's concrete
// Phase 2 header is TTyTk-InputProto.hpp, which supplies the event model.
enum EventMask : short { Readable = POLLIN, Writable = POLLOUT };

struct Event {
    enum class Kind { readable, writable, timer, idle, resize, signal };
    Kind kind{Kind::idle};
    int fd{-1};
    int signal_number{};
};

class Loop;

class Timer {
public:
    Timer() = default;
    void cancel() noexcept { active_ = false; }
    [[nodiscard]] auto active() const noexcept -> bool { return active_; }

private:
    friend class Loop;
    Timer(const std::size_t id, const bool active) : id_(id), active_(active) {}
    std::size_t id_{};
    bool active_{};
};

class Idle {
public:
    Idle() = default;
    void cancel() noexcept { active_ = false; }
    [[nodiscard]] auto active() const noexcept -> bool { return active_; }

private:
    friend class Loop;
    Idle(const std::size_t id, const bool active) : id_(id), active_(active) {}
    std::size_t id_{};
    bool active_{};
};

class FdWatch {
public:
    FdWatch() = default;
    void cancel() noexcept { active_ = false; }
    [[nodiscard]] auto active() const noexcept -> bool { return active_; }

private:
    friend class Loop;
    FdWatch(const std::size_t id, const bool active) : id_(id), active_(active) {}
    std::size_t id_{};
    bool active_{};
};

class Loop {
public:
    using Callback = std::function<void(const Event&)>;
    using Task = std::function<void()>;

    Loop() {
        if (::pipe(wakeup_) == 0) {
            (void)::fcntl(wakeup_[0], F_SETFL, O_NONBLOCK);
            watches_[0] = Watch{wakeup_[0], POLLIN, [this](const Event&) { drain_wakeup(); }, true};
        }
        current_ = this;
    }

    ~Loop() {
        stop();
        if (wakeup_[0] >= 0) ::close(wakeup_[0]);
        if (wakeup_[1] >= 0) ::close(wakeup_[1]);
        if (current_ == this) current_ = nullptr;
    }

    [[nodiscard]] auto watch(const int fd, const short events, Callback callback) -> FdWatch {
        const auto id = next_id_++;
        watches_[id] = Watch{fd, events, std::move(callback), true};
        return FdWatch{id, true};
    }

    [[nodiscard]] auto after(const std::chrono::milliseconds delay, Task callback) -> Timer {
        return schedule(delay, std::chrono::milliseconds::zero(), std::move(callback));
    }

    [[nodiscard]] auto every(const std::chrono::milliseconds interval, Task callback) -> Timer {
        return schedule(interval, interval, std::move(callback));
    }

    [[nodiscard]] auto idle(Task callback) -> Idle {
        const auto id = next_id_++;
        idles_.push_back({id, std::move(callback), true});
        return Idle{id, true};
    }

    void run_once(const std::chrono::milliseconds timeout = std::chrono::milliseconds{50}) {
        run_tasks();
        fire_timers();
        std::vector<::pollfd> descriptors;
        std::vector<std::size_t> ids;
        for (const auto& [id, watch] : watches_) {
            if (!watch.active) continue;
            descriptors.push_back({watch.fd, watch.events, 0});
            ids.push_back(id);
        }
        const auto result = ::poll(descriptors.data(), descriptors.size(),
                                   static_cast<int>(timeout.count()));
        if (result > 0) {
            for (std::size_t index = 0; index < descriptors.size(); ++index) {
                if (descriptors[index].revents == 0) continue;
                const auto found = watches_.find(ids[index]);
                if (found == watches_.end() || !found->second.active) continue;
                Event event{(descriptors[index].revents & POLLOUT) != 0
                                ? Event::Kind::writable : Event::Kind::readable,
                            found->second.fd};
                found->second.callback(event);
            }
        }
        for (auto& idle : idles_) if (idle.active) idle.callback();
        run_tasks();
    }

    void run() {
        running_ = true;
        while (running_) run_once();
    }

    void stop() noexcept { running_ = false; }
    [[nodiscard]] auto running() const noexcept -> bool { return running_; }
    [[nodiscard]] static auto current() noexcept -> Loop* { return current_; }

    void enqueue(Task task) {
        tasks_.push_back(std::move(task));
        if (wakeup_[1] >= 0) {
            const std::uint8_t byte = 1;
            (void)::write(wakeup_[1], &byte, sizeof(byte));
        }
    }

private:
    struct Watch {
        int fd;
        short events;
        Callback callback;
        bool active;
    };
    struct Scheduled {
        std::size_t id;
        std::chrono::steady_clock::time_point due;
        std::chrono::milliseconds repeat;
        Task callback;
        bool active;
    };
    struct IdleTask {
        std::size_t id;
        Task callback;
        bool active;
    };

    [[nodiscard]] auto schedule(const std::chrono::milliseconds delay,
                                const std::chrono::milliseconds repeat, Task callback) -> Timer {
        const auto id = next_id_++;
        timers_.push_back({id, std::chrono::steady_clock::now() + delay, repeat, std::move(callback), true});
        return Timer{id, true};
    }

    void fire_timers() {
        const auto now = std::chrono::steady_clock::now();
        for (auto& timer : timers_) {
            if (!timer.active || timer.due > now) continue;
            timer.callback();
            if (timer.repeat.count() == 0) timer.active = false;
            else timer.due = now + timer.repeat;
        }
    }

    void run_tasks() {
        while (!tasks_.empty()) {
            auto task = std::move(tasks_.front());
            tasks_.pop_front();
            task();
        }
    }

    void drain_wakeup() {
        std::uint8_t bytes[64]{};
        while (::read(wakeup_[0], bytes, sizeof(bytes)) > 0) {}
    }

    inline static Loop* current_{};
    int wakeup_[2]{-1, -1};
    std::size_t next_id_{1};
    std::unordered_map<std::size_t, Watch> watches_;
    std::vector<Scheduled> timers_;
    std::vector<IdleTask> idles_;
    std::deque<Task> tasks_;
    bool running_{};
};

inline void post(Loop& loop, Loop::Task task) { loop.enqueue(std::move(task)); }

// NOTE(agent): The manifest lists a zero-argument post() but does not specify
// loop ownership. This overload targets the most recently constructed loop.
inline void post(Loop::Task task) {
    if (Loop::current() != nullptr) Loop::current()->enqueue(std::move(task));
}

}  // namespace ttyutils::loop

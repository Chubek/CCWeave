#pragma once

#include <TTyUtils/TTyUtils-EventLoop.hpp>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace ttyutils::shell {

// NOTE(agent): The manifest names TTyTk::PseudoTerm; the repository freezes
// that header as TTyTk-PsuedoTerm.hpp. Non-PTY jobs use fork/exec here.
enum class Capture { None, Stdout, Stderr, Both };

class Job {
public:
    Job() = default;
    explicit Job(const pid_t pid) : pid_(pid) {}
    [[nodiscard]] auto wait() -> int {
        if (pid_ < 0) return -1;
        int status = 0;
        if (::waitpid(pid_, &status, 0) < 0) return -1;
        pid_ = -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    void terminate() noexcept { if (pid_ >= 0) ::kill(pid_, SIGTERM); }
    void on_line(std::function<void(std::string)> callback) { callback_ = std::move(callback); }
    [[nodiscard]] auto running() const -> bool {
        if (pid_ < 0) return false;
        return ::waitpid(pid_, nullptr, WNOHANG) == 0;
    }

private:
    pid_t pid_{-1};
    std::function<void(std::string)> callback_{};
};

struct Pipeline {
    std::vector<std::vector<std::string>> commands{};
};

class JobTable {
public:
    void add(Job job) { jobs_.push_back(std::move(job)); }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return jobs_.size(); }

private:
    std::vector<Job> jobs_;
};

inline auto run(const std::vector<std::string>& command, const Capture capture = Capture::None) -> Job {
    if (command.empty()) return {};
    const auto child = ::fork();
    if (child != 0) return child < 0 ? Job{} : Job{child};
    if (capture == Capture::Stdout || capture == Capture::Both) {
        const int sink = ::open("/dev/null", O_WRONLY);
        if (sink >= 0) ::dup2(sink, STDOUT_FILENO);
    }
    if (capture == Capture::Stderr || capture == Capture::Both) {
        const int sink = ::open("/dev/null", O_WRONLY);
        if (sink >= 0) ::dup2(sink, STDERR_FILENO);
    }
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (const auto& value : command) argv.push_back(const_cast<char*>(value.c_str()));
    argv.push_back(nullptr);
    ::execvp(argv.front(), argv.data());
    _exit(127);
}

}  // namespace ttyutils::shell

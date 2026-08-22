#pragma once

#include <TTyTk/TTyTk-Core.hpp>
#include <TTyTk/TTyTk-PsuedoTerm.hpp>
#include <TTyTk/TTyTk-SignalTools.hpp>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/types.h>
#include <unistd.h>

namespace ttytk::job {

[[nodiscard]] inline auto suspend(const int pid) -> Result<void> {
    if (::kill(static_cast<pid_t>(pid), SIGSTOP) != 0) {
        return Error{errno, std::strerror(errno)};
    }
    return {};
}

[[nodiscard]] inline auto resume(const int pid) -> Result<void> {
    if (::kill(static_cast<pid_t>(pid), SIGCONT) != 0) {
        return Error{errno, std::strerror(errno)};
    }
    return {};
}

[[nodiscard]] inline auto foreground(const int pid) -> Result<void> {
    // NOTE(agent): The manifest does not provide a terminal file descriptor,
    // so tcsetpgrp cannot be performed here. Establishing a process group and
    // resuming it is the conservative terminal-independent subset.
    const auto process = static_cast<pid_t>(pid);
    if (::setpgid(process, process) != 0 && errno != EACCES) {
        return Error{errno, std::strerror(errno)};
    }
    return resume(pid);
}

[[nodiscard]] inline auto background(const int pid) -> Result<void> {
    const auto process = static_cast<pid_t>(pid);
    if (::setpgid(process, process) != 0 && errno != EACCES) {
        return Error{errno, std::strerror(errno)};
    }
    return {};
}

}  // namespace ttytk::job

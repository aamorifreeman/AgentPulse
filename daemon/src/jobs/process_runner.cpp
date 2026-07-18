#include "jobs/process_runner.hpp"

#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>

extern char** environ;

namespace agentpulse {

namespace {

constexpr std::size_t kMaxCapture = 64 * 1024;  // per stream

std::int64_t now_unix() { return std::time(nullptr); }

// Appends up to the cap; sets `truncated` and returns false once full so the
// caller can stop reading that stream.
bool append_capped(std::string& buf, const char* data, std::size_t n,
                   bool& truncated) {
    if (buf.size() >= kMaxCapture) {
        truncated = true;
        return false;
    }
    std::size_t room = kMaxCapture - buf.size();
    std::size_t take = n < room ? n : room;
    buf.append(data, take);
    if (take < n) {
        truncated = true;
        return false;
    }
    return true;
}

}  // namespace

std::string to_string(RunStatus status) {
    switch (status) {
        case RunStatus::Success:    return "success";
        case RunStatus::Failed:     return "failed";
        case RunStatus::Timeout:    return "timeout";
        case RunStatus::SpawnError: return "spawn_error";
    }
    return "unknown";
}

RunResult run_command(const std::string& command, int timeout_seconds) {
    RunResult result;
    result.started_at = now_unix();
    const auto start = std::chrono::steady_clock::now();

    int out_pipe[2];
    int err_pipe[2];
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        result.error = std::string("pipe(): ") + std::strerror(errno);
        result.ended_at = now_unix();
        return result;
    }

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, err_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, out_pipe[0]);
    posix_spawn_file_actions_addclose(&fa, err_pipe[0]);
    posix_spawn_file_actions_addclose(&fa, out_pipe[1]);
    posix_spawn_file_actions_addclose(&fa, err_pipe[1]);

    // Put the child in its own process group so a timeout can kill the whole
    // tree (the shell plus any grandchildren) with kill(-pgid, ...).
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    pid_t pid = 0;
    int rc = posix_spawn(&pid, "/bin/sh", &fa, &attr,
                         const_cast<char* const*>(argv), environ);

    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    if (rc != 0) {
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        result.error = std::string("posix_spawn(): ") + std::strerror(rc);
        result.ended_at = now_unix();
        return result;
    }

    // Drain both streams until EOF or the deadline. On timeout, kill the group
    // but keep draining so we still capture whatever was produced.
    bool read_out = true, read_err = true;
    bool timed_out = false;
    bool killed = false;
    char buf[4096];

    while (read_out || read_err) {
        int timeout_ms = -1;
        if (timeout_seconds > 0 && !killed) {
            auto elapsed = std::chrono::steady_clock::now() - start;
            auto remaining =
                std::chrono::seconds(timeout_seconds) -
                std::chrono::duration_cast<std::chrono::seconds>(elapsed);
            long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          remaining)
                          .count();
            timeout_ms = ms > 0 ? static_cast<int>(ms) : 0;
        }

        pollfd fds[2];
        int n = 0;
        int out_idx = -1, err_idx = -1;
        if (read_out) { fds[n] = {out_pipe[0], POLLIN, 0}; out_idx = n++; }
        if (read_err) { fds[n] = {err_pipe[0], POLLIN, 0}; err_idx = n++; }

        int pr = ::poll(fds, n, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            // Deadline hit: kill the process group once, then finish draining.
            timed_out = true;
            if (!killed) {
                ::kill(-pid, SIGKILL);
                killed = true;
            }
            continue;
        }

        auto drain = [&](int idx, int fd, std::string& dst, bool& trunc,
                         bool& keep) {
            if (idx < 0) return;
            if (fds[idx].revents & (POLLIN | POLLHUP)) {
                ssize_t r = ::read(fd, buf, sizeof(buf));
                if (r <= 0) {
                    keep = false;  // EOF or error
                } else if (!append_capped(dst, buf, static_cast<std::size_t>(r),
                                          trunc)) {
                    keep = false;  // capture cap reached
                }
            }
        };
        drain(out_idx, out_pipe[0], result.stdout_text, result.stdout_truncated,
              read_out);
        drain(err_idx, err_pipe[0], result.stderr_text, result.stderr_truncated,
              read_err);
    }

    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    // If we stopped reading because of a full capture but the child is still
    // running, and a timeout applies, make sure it can't linger forever.
    int status = 0;
    ::waitpid(pid, &status, 0);

    auto elapsed = std::chrono::steady_clock::now() - start;
    result.duration_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    result.ended_at = now_unix();

    if (timed_out) {
        result.status = RunStatus::Timeout;
        result.exit_code = -SIGKILL;
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        result.status =
            result.exit_code == 0 ? RunStatus::Success : RunStatus::Failed;
    } else if (WIFSIGNALED(status)) {
        result.exit_code = -WTERMSIG(status);
        result.status = RunStatus::Failed;
    } else {
        result.status = RunStatus::Failed;
    }
    return result;
}

}  // namespace agentpulse

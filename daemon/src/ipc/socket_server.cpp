#include "ipc/socket_server.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "log.hpp"

namespace agentpulse {

namespace {
constexpr int kPollTimeoutMs = 200;
constexpr std::size_t kMaxRequestBytes = 64 * 1024;
}  // namespace

SocketServer::~SocketServer() {
    stop();
}

void SocketServer::start(const std::string& path, Handler handler) {
    if (path.size() >= sizeof(sockaddr_un::sun_path)) {
        throw std::runtime_error("socket path too long: " + path);
    }
    path_ = path;
    handler_ = std::move(handler);

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        throw std::runtime_error(std::string("socket(): ") +
                                 std::strerror(errno));
    }

    // Remove any stale socket left by a previous run before binding.
    ::unlink(path_.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) <
        0) {
        int e = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        throw std::runtime_error(std::string("bind(): ") + std::strerror(e));
    }

    // Restrict the socket to the owning user.
    ::chmod(path_.c_str(), 0600);

    if (::listen(listen_fd_, 8) < 0) {
        int e = errno;
        ::close(listen_fd_);
        listen_fd_ = -1;
        ::unlink(path_.c_str());
        throw std::runtime_error(std::string("listen(): ") + std::strerror(e));
    }

    running_ = true;
    thread_ = std::thread(&SocketServer::accept_loop, this);
    log_info("socket listening at " + path_);
}

void SocketServer::stop() {
    if (!running_.exchange(false)) {
        return;  // already stopped
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!path_.empty()) {
        ::unlink(path_.c_str());
    }
}

void SocketServer::accept_loop() {
    while (running_.load()) {
        pollfd pfd{listen_fd_, POLLIN, 0};
        int n = ::poll(&pfd, 1, kPollTimeoutMs);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_error(std::string("poll(): ") + std::strerror(errno));
            break;
        }
        if (n == 0 || (pfd.revents & POLLIN) == 0) {
            continue;  // timed out; re-check running_
        }

        int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK) {
                continue;
            }
            log_warn(std::string("accept(): ") + std::strerror(errno));
            continue;
        }
        serve_connection(client, handler_);
        ::close(client);
    }
}

void SocketServer::serve_connection(int client_fd, const Handler& handler) {
    // Read a single '\n'-terminated request line.
    std::string request;
    char buf[1024];
    bool complete = false;
    while (request.size() < kMaxRequestBytes) {
        ssize_t r = ::read(client_fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_warn(std::string("read(): ") + std::strerror(errno));
            return;
        }
        if (r == 0) {
            break;  // client closed
        }
        request.append(buf, static_cast<std::size_t>(r));
        if (auto pos = request.find('\n'); pos != std::string::npos) {
            request.resize(pos);  // drop newline and anything after
            complete = true;
            break;
        }
    }
    if (!complete) {
        return;
    }

    std::string response = handler(request);
    response.push_back('\n');

    // Write the full response, tolerating partial writes.
    std::size_t written = 0;
    while (written < response.size()) {
        ssize_t w = ::write(client_fd, response.data() + written,
                            response.size() - written);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            log_warn(std::string("write(): ") + std::strerror(errno));
            return;
        }
        written += static_cast<std::size_t>(w);
    }
}

}  // namespace agentpulse

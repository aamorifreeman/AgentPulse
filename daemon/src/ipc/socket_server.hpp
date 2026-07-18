#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace agentpulse {

// A tiny Unix-domain-socket server speaking newline-delimited JSON.
//
// Protocol: a client connects, writes one request line ending in '\n', and
// reads one response line ending in '\n'. One request per connection keeps
// the model trivial and matches a menu-bar app that polls periodically.
//
// The accept loop runs on its own thread and polls with a timeout so it can
// observe stop() promptly without a self-pipe.
class SocketServer {
public:
    // Handler maps a request line (without trailing newline) to a response
    // line (also without newline). It is invoked on the accept thread.
    using Handler = std::function<std::string(const std::string&)>;

    SocketServer() = default;
    ~SocketServer();

    SocketServer(const SocketServer&) = delete;
    SocketServer& operator=(const SocketServer&) = delete;

    // Binds `path` (unlinking any stale socket first), starts listening, and
    // spawns the accept thread. Throws std::runtime_error on failure.
    void start(const std::string& path, Handler handler);

    // Stops the accept thread, closes the socket, and removes the path.
    void stop();

    // Reads one '\n'-terminated request from an already-connected socket fd,
    // invokes `handler`, and writes the '\n'-terminated response. Exposed
    // (and static) so it can be exercised over a socketpair in tests without
    // binding a listening socket.
    static void serve_connection(int fd, const Handler& handler);

private:
    void accept_loop();

    std::string path_;
    Handler handler_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace agentpulse

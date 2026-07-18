// apctl — AgentPulse control client.
//
// Connects to the daemon's Unix-domain socket, sends one command line, and
// prints the JSON response. `--watch` repeats a "status" request on an
// interval for a live view.

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

std::string default_socket_path() {
    const char* home = std::getenv("HOME");
    std::string base = home ? home : ".";
    return base + "/Library/Application Support/AgentPulse/agentpulse.sock";
}

// Sends one request line and returns the response line (newline stripped).
// Throws std::runtime_error on connection or I/O failure.
std::string send_request(const std::string& socket_path,
                         const std::string& command) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("socket(): ") +
                                 std::strerror(errno));
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        throw std::runtime_error("socket path too long");
    }
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        int e = errno;
        ::close(fd);
        throw std::runtime_error("connect(" + socket_path + "): " +
                                 std::strerror(e));
    }

    std::string line = command + "\n";
    std::size_t written = 0;
    while (written < line.size()) {
        ssize_t w = ::write(fd, line.data() + written, line.size() - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            throw std::runtime_error(std::string("write(): ") +
                                     std::strerror(errno));
        }
        written += static_cast<std::size_t>(w);
    }

    std::string response;
    char buf[1024];
    while (true) {
        ssize_t r = ::read(fd, buf, sizeof(buf));
        if (r < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            throw std::runtime_error(std::string("read(): ") +
                                     std::strerror(errno));
        }
        if (r == 0) break;
        response.append(buf, static_cast<std::size_t>(r));
        if (auto pos = response.find('\n'); pos != std::string::npos) {
            response.resize(pos);
            break;
        }
    }
    ::close(fd);
    return response;
}

void print_response(const std::string& response) {
    auto parsed = json::parse(response, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        std::cout << response << "\n";  // print raw if not valid JSON
    } else {
        std::cout << parsed.dump(2) << "\n";
    }
}

void usage() {
    std::cerr <<
        "Usage: apctl [--socket PATH] [--watch [SECONDS]] [COMMAND]\n"
        "  COMMAND   command to send (default: status). e.g. ping, status\n"
        "  --socket  path to the daemon socket\n"
        "  --watch   repeat a status request every SECONDS (default 2)\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string socket_path = default_socket_path();
    std::string command = "status";
    bool watch = false;
    int watch_seconds = 2;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg == "--socket") {
            if (++i >= argc) { usage(); return 2; }
            socket_path = argv[i];
        } else if (arg == "--watch") {
            watch = true;
            // Optional numeric argument follows --watch.
            if (i + 1 < argc && std::isdigit(static_cast<unsigned char>(argv[i + 1][0]))) {
                watch_seconds = std::atoi(argv[++i]);
                if (watch_seconds <= 0) watch_seconds = 2;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            usage();
            return 2;
        } else {
            command = arg;
        }
    }

    try {
        if (watch) {
            command = "status";
            while (true) {
                std::string response = send_request(socket_path, command);
                std::cout << "\x1b[2J\x1b[H";  // clear screen, home cursor
                print_response(response);
                std::this_thread::sleep_for(std::chrono::seconds(watch_seconds));
            }
        } else {
            print_response(send_request(socket_path, command));
        }
    } catch (const std::exception& e) {
        std::cerr << "apctl: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

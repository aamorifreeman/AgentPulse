import Foundation

// A minimal blocking Unix-domain-socket client for the AgentPulse daemon.
// One request per connection: write a command line, read one '\n'-terminated
// JSON response. URLSession can't speak Unix sockets, so we use POSIX
// directly. Calls block, so invoke off the main thread.
struct SocketClient: Sendable {
    enum ClientError: Error, CustomStringConvertible {
        case socketFailed
        case connectFailed(String)
        case ioFailed(String)

        var description: String {
            switch self {
            case .socketFailed: return "could not create socket"
            case .connectFailed(let m): return "connect failed: \(m)"
            case .ioFailed(let m): return "I/O failed: \(m)"
            }
        }
    }

    let path: String

    static func defaultPath() -> String {
        let home = FileManager.default.homeDirectoryForCurrentUser.path
        return home + "/Library/Application Support/AgentPulse/agentpulse.sock"
    }

    /// Sends `command` and returns the response line (newline stripped).
    func send(_ command: String) throws -> Data {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        if fd < 0 { throw ClientError.socketFailed }
        defer { close(fd) }

        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let capacity = MemoryLayout.size(ofValue: addr.sun_path)
        let ok = path.withCString { src -> Bool in
            let srcLen = strlen(src)
            if srcLen >= capacity { return false }
            _ = withUnsafeMutablePointer(to: &addr.sun_path) { rawPtr in
                rawPtr.withMemoryRebound(to: CChar.self, capacity: capacity) { dst in
                    memcpy(dst, src, srcLen + 1)
                }
            }
            return true
        }
        if !ok { throw ClientError.connectFailed("path too long") }

        let len = socklen_t(MemoryLayout<sockaddr_un>.size)
        let rc = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                connect(fd, sa, len)
            }
        }
        if rc != 0 {
            throw ClientError.connectFailed(String(cString: strerror(errno)))
        }

        // Write the request line.
        var request = command + "\n"
        try request.withUTF8 { buf in
            var offset = 0
            while offset < buf.count {
                let n = write(fd, buf.baseAddress! + offset, buf.count - offset)
                if n <= 0 {
                    throw ClientError.ioFailed(String(cString: strerror(errno)))
                }
                offset += n
            }
        }

        // Read until newline or EOF.
        var data = Data()
        var chunk = [UInt8](repeating: 0, count: 8192)
        while true {
            let n = read(fd, &chunk, chunk.count)
            if n < 0 { throw ClientError.ioFailed(String(cString: strerror(errno))) }
            if n == 0 { break }
            data.append(chunk, count: n)
            if let nl = data.firstIndex(of: 0x0A) {
                return data.prefix(upTo: nl)
            }
        }
        return data
    }
}

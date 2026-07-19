import Foundation

// Codable mirrors of the daemon's JSON API (see docs/PROTOCOL.md). The decoder
// uses .convertFromSnakeCase, so JSON keys like "used_percent" map to
// usedPercent, "top_processes" to topProcesses, etc.

struct StatusResponse: Codable {
    let ok: Bool
    let daemon: DaemonInfo?
    let cpu: CpuInfo?
    let system: SystemInfo?
    let jobs: [JobStatus]?
    let alerts: [AlertInfo]?
}

struct DaemonInfo: Codable {
    let startedAt: Int
    let rssBytes: UInt64?
    let cpuPercent: Double?
}

struct CpuInfo: Codable {
    let valid: Bool
    let percent: Double
    let sampledAt: Int
}

struct SystemInfo: Codable {
    let valid: Bool
    let sampledAt: Int
    let memory: MemoryInfo
    let disk: DiskInfo
    let thermalState: String
    let topProcesses: [ProcInfo]
}

struct MemoryInfo: Codable {
    let totalBytes: UInt64
    let usedBytes: UInt64
    let usedPercent: Double
}

struct DiskInfo: Codable {
    let totalBytes: UInt64
    let availableBytes: UInt64
    let usedPercent: Double
}

struct ProcInfo: Codable, Identifiable {
    let pid: Int
    let name: String
    let cpuPercent: Double
    let rssBytes: UInt64
    var id: Int { pid }
}

struct JobStatus: Codable, Identifiable {
    let name: String
    let schedule: String
    let source: String?
    let nextRun: Int
    let running: Bool
    let lastRun: LastRun?
    var id: String { name }
    var isUserManaged: Bool { source == "ui" }
}

struct LastRun: Codable {
    let status: String
    let startedAt: Int
    let exitCode: Int
    let durationMs: Int
    let trigger: String
}

struct AlertInfo: Codable, Identifiable {
    let ts: Int
    let rule: String
    let severity: String
    let metric: String
    let kind: String
    let value: Double
    let threshold: Double
    let message: String
    let attribution: String
    let notified: Bool
    var id: String { "\(ts)-\(rule)-\(kind)" }
}

enum API {
    static func decodeStatus(_ data: Data) throws -> StatusResponse {
        let decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        return try decoder.decode(StatusResponse.self, from: data)
    }
}

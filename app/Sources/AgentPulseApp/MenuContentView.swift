import Foundation
import SwiftUI

// Small formatting helpers shared by the views.
enum Fmt {
    static func bytes(_ v: UInt64) -> String {
        let f = ByteCountFormatter()
        f.countStyle = .memory
        return f.string(fromByteCount: Int64(v))
    }

    static func gb(_ v: UInt64) -> String {
        String(format: "%.1f GB", Double(v) / 1_000_000_000.0)
    }

    static func pct(_ v: Double) -> String { String(format: "%.0f%%", v) }

    static func ago(_ unix: Int) -> String {
        if unix == 0 { return "never" }
        let secs = Int(Date().timeIntervalSince1970) - unix
        if secs < 0 { return "in \(-secs)s" }
        if secs < 60 { return "\(secs)s ago" }
        if secs < 3600 { return "\(secs / 60)m ago" }
        if secs < 86400 { return "\(secs / 3600)h ago" }
        return "\(secs / 86400)d ago"
    }

    static func at(_ unix: Int) -> String {
        if unix == 0 { return "—" }
        let df = DateFormatter()
        df.dateFormat = "MMM d, HH:mm"
        return df.string(from: Date(timeIntervalSince1970: TimeInterval(unix)))
    }

    static func thermalColor(_ s: String) -> Color {
        switch s {
        case "fair": return .yellow
        case "serious": return .orange
        case "critical": return .red
        default: return .green
        }
    }

    static func statusColor(_ s: String) -> Color {
        switch s {
        case "success": return .green
        case "running": return .blue
        case "timeout", "failed", "spawn_error": return .red
        default: return .secondary
        }
    }

    static func severityColor(_ s: String) -> Color {
        switch s {
        case "critical": return .red
        case "serious": return .orange
        default: return .yellow
        }
    }
}

struct MenuContentView: View {
    @EnvironmentObject var store: StatusStore

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            header
            Divider()

            if let system = store.status?.system, system.valid {
                systemSection(system)
                Divider()
                processSection(system.topProcesses)
                Divider()
            } else if store.lastError != nil {
                Text("Daemon not reachable")
                    .foregroundStyle(.secondary)
                Text("Is agentpulsed running?")
                    .font(.caption).foregroundStyle(.secondary)
                Divider()
            } else {
                ProgressView().padding(.vertical, 4)
            }

            jobsSection
            alertsSection

            Divider()
            footer
        }
        .padding(12)
        .frame(width: 340)
        .task { store.start() }
    }

    private var header: some View {
        HStack {
            Image(systemName: "waveform.path.ecg")
            Text("AgentPulse").font(.headline)
            Spacer()
            Circle()
                .fill(store.connected ? Color.green : Color.red)
                .frame(width: 8, height: 8)
            if let d = store.status?.daemon, let rss = d.rssBytes {
                Text(Fmt.bytes(rss)).font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private func systemSection(_ s: SystemInfo) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("SYSTEM").font(.caption).foregroundStyle(.secondary)
            metricRow("CPU", value: Fmt.pct(store.status?.cpu?.percent ?? 0))
            metricRow("Memory",
                      value: "\(Fmt.pct(s.memory.usedPercent)) of \(Fmt.gb(s.memory.totalBytes))")
            metricRow("Disk free", value: Fmt.gb(s.disk.availableBytes))
            HStack {
                Text("Thermal").frame(width: 90, alignment: .leading)
                Text(s.thermalState.capitalized)
                    .foregroundStyle(Fmt.thermalColor(s.thermalState))
                Spacer()
            }
        }
    }

    private func metricRow(_ label: String, value: String) -> some View {
        HStack {
            Text(label).frame(width: 90, alignment: .leading)
            Text(value)
            Spacer()
        }
    }

    private func processSection(_ procs: [ProcInfo]) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("TOP PROCESSES").font(.caption).foregroundStyle(.secondary)
            ForEach(procs.prefix(5)) { p in
                HStack {
                    Text(p.name).lineLimit(1)
                    Spacer()
                    Text(Fmt.pct(p.cpuPercent)).foregroundStyle(.secondary)
                        .font(.caption.monospacedDigit())
                }
            }
        }
    }

    @ViewBuilder private var jobsSection: some View {
        let jobs = store.status?.jobs ?? []
        VStack(alignment: .leading, spacing: 6) {
            Text("AUTOMATIONS").font(.caption).foregroundStyle(.secondary)
            if jobs.isEmpty {
                Text("No automations yet — add one below.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            ForEach(jobs) { job in
                jobRow(job)
            }
            AddJobForm()
        }
        Divider()
    }

    private func jobRow(_ job: JobStatus) -> some View {
        HStack(alignment: .top) {
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Circle()
                        .fill(job.running
                              ? Color.blue
                              : Fmt.statusColor(job.lastRun?.status ?? ""))
                        .frame(width: 7, height: 7)
                    Text(job.name).fontWeight(.medium).lineLimit(1)
                }
                if job.running {
                    Text("running…").font(.caption).foregroundStyle(.blue)
                } else if let last = job.lastRun {
                    Text("\(last.status) · \(Fmt.ago(last.startedAt))")
                        .font(.caption).foregroundStyle(.secondary)
                } else {
                    Text("no runs yet").font(.caption).foregroundStyle(.secondary)
                }
            }
            Spacer()
            Button(job.lastRun?.status == "success" || job.lastRun == nil
                   ? "Run" : "Retry") {
                store.run(job.name)
            }
            .buttonStyle(.borderless)
            .disabled(job.running)
            if job.isUserManaged {
                Button {
                    store.removeJob(job.name)
                } label: {
                    Image(systemName: "trash").foregroundStyle(.red)
                }
                .buttonStyle(.borderless)
                .help("Remove automation")
            }
        }
    }

    @ViewBuilder private var alertsSection: some View {
        let alerts = (store.status?.alerts ?? []).prefix(5)
        if !alerts.isEmpty {
            VStack(alignment: .leading, spacing: 4) {
                Text("RECENT ALERTS").font(.caption).foregroundStyle(.secondary)
                ForEach(Array(alerts)) { a in
                    HStack(alignment: .top, spacing: 6) {
                        Image(systemName: a.kind == "recovered"
                              ? "checkmark.circle" : "exclamationmark.triangle")
                            .foregroundStyle(a.kind == "recovered"
                                             ? .green : Fmt.severityColor(a.severity))
                            .font(.caption)
                        VStack(alignment: .leading, spacing: 1) {
                            Text(a.rule).font(.caption).fontWeight(.medium)
                            Text(a.kind == "recovered" ? "recovered" : a.message)
                                .font(.caption2).foregroundStyle(.secondary)
                                .lineLimit(2)
                        }
                        Spacer()
                        Text(Fmt.ago(a.ts)).font(.caption2).foregroundStyle(.secondary)
                    }
                }
            }
            Divider()
        }
    }

    private var footer: some View {
        HStack {
            Button("Refresh") { store.refresh() }
                .buttonStyle(.borderless)
            Spacer()
            Button("Quit") { NSApplication.shared.terminate(nil) }
                .buttonStyle(.borderless)
        }
    }
}

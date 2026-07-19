import SwiftUI

// The main application window: a full dashboard with system health cards,
// automations (add/run/retry/remove), top processes, and recent alerts.
struct DashboardView: View {
    @EnvironmentObject var store: StatusStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                header
                systemCards
                HStack(alignment: .top, spacing: 16) {
                    automationsPanel
                    processesPanel
                }
                alertsPanel
            }
            .padding(20)
        }
        .frame(minWidth: 760, minHeight: 580)
        .task { store.start() }
    }

    // MARK: Header

    private var header: some View {
        HStack(spacing: 10) {
            Image(systemName: "waveform.path.ecg")
                .font(.title2).foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 1) {
                Text("AgentPulse").font(.title2.bold())
                Text("macOS automation & system monitor")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 2) {
                HStack(spacing: 5) {
                    Circle().fill(store.connected ? .green : .red)
                        .frame(width: 8, height: 8)
                    Text(store.connected ? "Connected" : "Daemon offline")
                        .font(.caption)
                }
                if let d = store.status?.daemon, let rss = d.rssBytes {
                    Text("daemon \(Fmt.bytes(rss)) · \(Fmt.pct(d.cpuPercent ?? 0))")
                        .font(.caption2).foregroundStyle(.secondary)
                }
            }
            Button {
                store.refresh()
            } label: {
                Image(systemName: "arrow.clockwise")
            }
            .help("Refresh now")
        }
    }

    // MARK: System cards

    private var systemCards: some View {
        let sys = store.status?.system
        return LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 12),
                                        count: 4),
                         spacing: 12) {
            MetricCard(title: "CPU", value: Fmt.pct(store.status?.cpu?.percent ?? 0),
                       systemImage: "cpu", tint: .blue)
            MetricCard(title: "Memory",
                       value: sys.map { Fmt.pct($0.memory.usedPercent) } ?? "—",
                       subtitle: sys.map { "of \(Fmt.gb($0.memory.totalBytes))" },
                       systemImage: "memorychip", tint: .purple)
            MetricCard(title: "Disk free",
                       value: sys.map { Fmt.gb($0.disk.availableBytes) } ?? "—",
                       subtitle: sys.map { "\(Fmt.pct($0.disk.usedPercent)) used" },
                       systemImage: "internaldrive", tint: .teal)
            MetricCard(title: "Thermal",
                       value: (sys?.thermalState ?? "—").capitalized,
                       systemImage: "thermometer.medium",
                       tint: Fmt.thermalColor(sys?.thermalState ?? "nominal"))
        }
    }

    // MARK: Automations

    private var automationsPanel: some View {
        Panel(title: "Automations", systemImage: "bolt.badge.clock") {
            let jobs = store.status?.jobs ?? []
            if jobs.isEmpty {
                Text("No automations yet — add one below.")
                    .font(.callout).foregroundStyle(.secondary)
                    .padding(.vertical, 4)
            }
            ForEach(jobs) { job in
                JobRow(job: job)
                if job.id != jobs.last?.id { Divider() }
            }
            Divider().padding(.vertical, 2)
            AddJobForm()
        }
    }

    // MARK: Processes

    private var processesPanel: some View {
        Panel(title: "Top processes", systemImage: "list.bullet") {
            let procs = store.status?.system?.topProcesses ?? []
            if procs.isEmpty {
                Text("—").foregroundStyle(.secondary)
            }
            ForEach(procs.prefix(8)) { p in
                HStack {
                    Text(p.name).lineLimit(1)
                    Spacer()
                    Text(Fmt.bytes(p.rssBytes)).font(.caption)
                        .foregroundStyle(.secondary)
                    Text(Fmt.pct(p.cpuPercent))
                        .font(.caption.monospacedDigit())
                        .frame(width: 52, alignment: .trailing)
                }
            }
        }
    }

    // MARK: Alerts

    private var alertsPanel: some View {
        Panel(title: "Recent alerts", systemImage: "bell") {
            let alerts = store.status?.alerts ?? []
            if alerts.isEmpty {
                Text("No alerts.").font(.callout).foregroundStyle(.secondary)
            }
            ForEach(alerts.prefix(8)) { a in
                HStack(alignment: .top, spacing: 8) {
                    Image(systemName: a.kind == "recovered"
                          ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                        .foregroundStyle(a.kind == "recovered"
                                         ? .green : Fmt.severityColor(a.severity))
                    VStack(alignment: .leading, spacing: 1) {
                        Text(a.rule).fontWeight(.medium)
                        Text(a.kind == "recovered" ? "recovered" : a.message)
                            .font(.caption).foregroundStyle(.secondary).lineLimit(2)
                        if !a.attribution.isEmpty {
                            Text(a.attribution).font(.caption2).foregroundStyle(.secondary)
                        }
                    }
                    Spacer()
                    Text(Fmt.ago(a.ts)).font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }
}

// MARK: - Reusable pieces

// A titled panel with a subtle background.
struct Panel<Content: View>: View {
    let title: String
    let systemImage: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label(title, systemImage: systemImage)
                .font(.headline)
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(14)
        .background(RoundedRectangle(cornerRadius: 12)
            .fill(Color(nsColor: .controlBackgroundColor)))
    }
}

struct MetricCard: View {
    let title: String
    let value: String
    var subtitle: String? = nil
    let systemImage: String
    let tint: Color

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(title, systemImage: systemImage)
                .font(.caption).foregroundStyle(.secondary)
                .labelStyle(.titleAndIcon)
            Text(value).font(.title3.bold()).foregroundStyle(tint)
            Text(subtitle ?? " ").font(.caption2).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 12)
            .fill(Color(nsColor: .controlBackgroundColor)))
    }
}

// A single automation row with Run/Retry and (for UI-managed jobs) delete.
struct JobRow: View {
    @EnvironmentObject var store: StatusStore
    let job: JobStatus

    var body: some View {
        HStack(alignment: .top) {
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 6) {
                    Circle()
                        .fill(job.running ? Color.blue
                              : Fmt.statusColor(job.lastRun?.status ?? ""))
                        .frame(width: 8, height: 8)
                    Text(job.name).fontWeight(.medium)
                    if !job.schedule.isEmpty {
                        Text(job.schedule).font(.caption2.monospaced())
                            .foregroundStyle(.secondary)
                    }
                }
                if job.running {
                    Text("running…").font(.caption).foregroundStyle(.blue)
                } else if let last = job.lastRun {
                    Text("\(last.status) · \(Fmt.ago(last.startedAt)) · \(last.durationMs)ms")
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
            .disabled(job.running)
            if job.isUserManaged {
                Button(role: .destructive) {
                    store.removeJob(job.name)
                } label: {
                    Image(systemName: "trash")
                }
                .help("Remove automation")
            }
        }
        .padding(.vertical, 3)
    }
}

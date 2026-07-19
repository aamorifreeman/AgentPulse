import Foundation
import SwiftUI

// Polls the daemon for status on an interval and publishes it to the UI.
// Socket I/O runs off the main actor; results are applied on the main actor.
@MainActor
final class StatusStore: ObservableObject {
    @Published var status: StatusResponse?
    @Published var lastError: String?
    @Published var connected = false
    @Published var actionError: String?

    private let client: SocketClient
    private var timer: Timer?
    private var seededAlerts = false
    private var seenAlertIDs = Set<String>()

    init(path: String = SocketClient.defaultPath()) {
        self.client = SocketClient(path: path)
    }

    func start() {
        Notifier.requestAuthorization()
        refresh()
        let t = Timer.scheduledTimer(withTimeInterval: 3.0, repeats: true) {
            [weak self] _ in
            Task { @MainActor in self?.refresh() }
        }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    func stop() {
        timer?.invalidate()
        timer = nil
    }

    func refresh() {
        let client = self.client
        Task.detached {
            do {
                let data = try client.send("status")
                let status = try API.decodeStatus(data)
                await self.apply(status)
            } catch {
                await self.applyError(String(describing: error))
            }
        }
    }

    /// Triggers a job run, then refreshes shortly after.
    func run(_ job: String) {
        let client = self.client
        Task.detached {
            _ = try? client.send("run \(job)")
            try? await Task.sleep(nanoseconds: 500_000_000)
            await self.refresh()
        }
    }

    /// Adds a new automation. Reports failures via `actionError`.
    func addJob(name: String, command: String, schedule: String,
                timeoutSeconds: Int, retries: Int, missedPolicy: String) {
        var body: [String: Any] = [
            "cmd": "add_job",
            "name": name,
            "command": command,
            "timeout_seconds": timeoutSeconds,
            "retries": retries,
            "missed_run_policy": missedPolicy,
        ]
        let trimmed = schedule.trimmingCharacters(in: .whitespaces)
        if !trimmed.isEmpty { body["schedule"] = trimmed }
        sendCommand(body)
    }

    /// Removes a user-managed automation.
    func removeJob(_ name: String) {
        sendCommand(["cmd": "remove_job", "job": name])
    }

    // Serializes a command dict to JSON, sends it, and surfaces any error.
    private func sendCommand(_ body: [String: Any]) {
        guard let data = try? JSONSerialization.data(withJSONObject: body),
              let line = String(data: data, encoding: .utf8) else {
            actionError = "could not encode request"
            return
        }
        let client = self.client
        Task.detached {
            do {
                let resp = try client.send(line)
                let obj = try JSONSerialization.jsonObject(with: resp)
                    as? [String: Any]
                let ok = obj?["ok"] as? Bool ?? false
                let err = obj?["error"] as? String
                await self.finishAction(ok: ok, error: err)
            } catch {
                await self.finishAction(ok: false,
                                        error: String(describing: error))
            }
        }
    }

    private func finishAction(ok: Bool, error: String?) {
        actionError = ok ? nil : (error ?? "request failed")
        refresh()
    }

    private func apply(_ status: StatusResponse) {
        self.status = status
        self.connected = true
        self.lastError = nil
        notifyNewAlerts(status.alerts ?? [])
    }

    private func applyError(_ message: String) {
        self.connected = false
        self.lastError = message
    }

    // On first successful fetch, mark all current alerts as seen so we don't
    // notify for backlog. After that, post a notification for each new alert
    // the daemon flagged as notifiable.
    private func notifyNewAlerts(_ alerts: [AlertInfo]) {
        if !seededAlerts {
            for a in alerts { seenAlertIDs.insert(a.id) }
            seededAlerts = true
            return
        }
        for a in alerts where !seenAlertIDs.contains(a.id) {
            seenAlertIDs.insert(a.id)
            if a.notified && a.kind == "firing" {
                let title = "AgentPulse: \(a.rule)"
                var body = a.message
                if !a.attribution.isEmpty { body += "\n" + a.attribution }
                Notifier.post(title: title, body: body)
            }
        }
    }
}

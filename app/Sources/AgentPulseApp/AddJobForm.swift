import SwiftUI

// A compact form (collapsed by default) for adding a new automation. Sends to
// the daemon via StatusStore.addJob.
struct AddJobForm: View {
    @EnvironmentObject var store: StatusStore
    @State private var expanded = false
    @State private var name = ""
    @State private var command = ""
    @State private var schedule = ""
    @State private var timeout = ""
    @State private var retries = ""
    @State private var missedPolicy = "none"

    private let policies = ["none", "run_on_wake", "run_now"]

    private var canAdd: Bool {
        !name.trimmingCharacters(in: .whitespaces).isEmpty
            && !command.trimmingCharacters(in: .whitespaces).isEmpty
    }

    var body: some View {
        DisclosureGroup(isExpanded: $expanded) {
            VStack(alignment: .leading, spacing: 6) {
                TextField("Name (e.g. email-scan)", text: $name)
                TextField("Command (e.g. python3 scan.py)", text: $command)
                TextField("Schedule — cron, optional (0 8 * * *)", text: $schedule)
                HStack {
                    TextField("Timeout s", text: $timeout)
                        .frame(width: 80)
                    TextField("Retries", text: $retries)
                        .frame(width: 70)
                    Picker("", selection: $missedPolicy) {
                        ForEach(policies, id: \.self) { Text($0).tag($0) }
                    }
                    .labelsHidden()
                }
                if let err = store.actionError {
                    Text(err).font(.caption).foregroundStyle(.red).lineLimit(2)
                }
                HStack {
                    Spacer()
                    Button("Add automation") { add() }
                        .disabled(!canAdd)
                }
            }
            .textFieldStyle(.roundedBorder)
            .padding(.top, 4)
        } label: {
            Label("Add automation", systemImage: "plus.circle")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private func add() {
        store.addJob(
            name: name.trimmingCharacters(in: .whitespaces),
            command: command.trimmingCharacters(in: .whitespaces),
            schedule: schedule,
            timeoutSeconds: Int(timeout) ?? 0,
            retries: Int(retries) ?? 0,
            missedPolicy: missedPolicy)
        // Clear on success (actionError nil means it went through after refresh).
        name = ""; command = ""; schedule = ""; timeout = ""; retries = ""
        missedPolicy = "none"
    }
}

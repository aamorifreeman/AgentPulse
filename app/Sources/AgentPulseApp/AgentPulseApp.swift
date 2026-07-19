import SwiftUI
import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Regular app: dock icon + main window (plus the menu-bar extra).
        NSApp.setActivationPolicy(.regular)
    }

    // Reopen the dashboard window when the dock icon is clicked.
    func applicationShouldHandleReopen(_ sender: NSApplication,
                                       hasVisibleWindows flag: Bool) -> Bool {
        if !flag { NSApp.activate(ignoringOtherApps: true) }
        return true
    }
}

@main
struct AgentPulseApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var store = StatusStore()

    var body: some Scene {
        WindowGroup("AgentPulse") {
            DashboardView().environmentObject(store)
        }
        .windowResizability(.contentSize)

        // Bonus: a quick-glance menu-bar item backed by the same store.
        MenuBarExtra {
            MenuContentView().environmentObject(store)
        } label: {
            menuBarLabel
        }
        .menuBarExtraStyle(.window)
    }

    private var menuBarLabel: some View {
        HStack(spacing: 3) {
            Image(systemName: "waveform.path.ecg")
            if let cpu = store.status?.cpu, cpu.valid {
                Text(Fmt.pct(cpu.percent))
            }
        }
    }
}

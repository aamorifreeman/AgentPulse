import SwiftUI
import AppKit

// Hides the Dock icon so AgentPulse lives only in the menu bar. When packaged
// with make-app.sh, Info.plist's LSUIElement does the same; this covers the
// bare-executable case too.
final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
    }
}

@main
struct AgentPulseApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var store = StatusStore()

    var body: some Scene {
        MenuBarExtra {
            MenuContentView().environmentObject(store)
        } label: {
            menuBarLabel
        }
        .menuBarExtraStyle(.window)
    }

    // Shows a compact CPU readout beside the icon when data is available.
    private var menuBarLabel: some View {
        HStack(spacing: 3) {
            Image(systemName: "waveform.path.ecg")
            if let cpu = store.status?.cpu, cpu.valid {
                Text(Fmt.pct(cpu.percent))
            }
        }
    }
}

import Foundation
import UserNotifications

// Wraps UserNotifications. UNUserNotificationCenter requires a bundle
// identifier, so when the executable is run without an .app bundle (e.g. via
// `swift run`) notifications are silently skipped. Build with make-app.sh for
// working notifications.
enum Notifier {
    static var available: Bool { Bundle.main.bundleIdentifier != nil }

    static func requestAuthorization() {
        guard available else { return }
        UNUserNotificationCenter.current().requestAuthorization(
            options: [.alert, .sound]
        ) { _, _ in }
    }

    static func post(title: String, body: String) {
        guard available else { return }
        let content = UNMutableNotificationContent()
        content.title = title
        content.body = body
        content.sound = .default
        let request = UNNotificationRequest(
            identifier: UUID().uuidString, content: content, trigger: nil)
        UNUserNotificationCenter.current().add(request)
    }
}

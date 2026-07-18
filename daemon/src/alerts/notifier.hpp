#pragma once

#include <string>

namespace agentpulse {

// Posts a macOS notification via osascript. Best-effort: failures are logged,
// never fatal. This is the interim backend until the SwiftUI menu-bar app
// delivers richer, actionable notifications through UserNotifications.
void send_notification(const std::string& title, const std::string& message);

}  // namespace agentpulse

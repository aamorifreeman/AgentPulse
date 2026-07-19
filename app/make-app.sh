#!/usr/bin/env bash
# Build the menu-bar app and wrap it in a proper .app bundle (LSUIElement so
# it lives only in the menu bar, and a bundle id so UserNotifications work).
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NAME="AgentPulse"
BUNDLE_ID="com.aamorifreeman.agentpulse.menubar"
OUT="$APP_DIR/$NAME.app"

echo "==> Building release binary"
# --disable-sandbox: SwiftPM's manifest sandbox can conflict with some CI /
# sandboxed shells; harmless locally.
swift build -c release --disable-sandbox --package-path "$APP_DIR"
BIN="$(swift build -c release --disable-sandbox --package-path "$APP_DIR" --show-bin-path)/AgentPulseApp"

echo "==> Assembling $OUT"
rm -rf "$OUT"
mkdir -p "$OUT/Contents/MacOS"
install -m 0755 "$BIN" "$OUT/Contents/MacOS/$NAME"

cat > "$OUT/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>            <string>$NAME</string>
    <key>CFBundleDisplayName</key>     <string>$NAME</string>
    <key>CFBundleIdentifier</key>      <string>$BUNDLE_ID</string>
    <key>CFBundleExecutable</key>      <string>$NAME</string>
    <key>CFBundlePackageType</key>     <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>0.1.0</string>
    <key>LSMinimumSystemVersion</key>  <string>13.0</string>
</dict>
</plist>
PLIST

echo "==> Built $OUT"
echo "    Launch it with:  open \"$OUT\""

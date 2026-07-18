#!/usr/bin/env bash
# Build agentpulsed, install it, and load it as a per-user LaunchAgent.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
PREFIX="$HOME/.local"
BIN_DIR="$PREFIX/bin"
PROGRAM="$BIN_DIR/agentpulsed"

LABEL="com.aamorifreeman.agentpulse"
LAUNCH_AGENTS="$HOME/Library/LaunchAgents"
PLIST_DEST="$LAUNCH_AGENTS/$LABEL.plist"
PLIST_TEMPLATE="$REPO_ROOT/packaging/$LABEL.plist.in"
LOG_DIR="$HOME/Library/Logs/AgentPulse"

echo "==> Configuring & building (Release)"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" --parallel

echo "==> Installing binary to $PROGRAM"
mkdir -p "$BIN_DIR" "$LOG_DIR" "$LAUNCH_AGENTS"
install -m 0755 "$BUILD_DIR/daemon/agentpulsed" "$PROGRAM"

echo "==> Writing LaunchAgent to $PLIST_DEST"
sed -e "s|__PROGRAM__|$PROGRAM|g" \
    -e "s|__STDOUT__|$LOG_DIR/agentpulsed.out.log|g" \
    -e "s|__STDERR__|$LOG_DIR/agentpulsed.err.log|g" \
    "$PLIST_TEMPLATE" > "$PLIST_DEST"

echo "==> (Re)loading LaunchAgent"
GUI="gui/$(id -u)"
launchctl bootout "$GUI/$LABEL" 2>/dev/null || true
launchctl bootstrap "$GUI" "$PLIST_DEST"
launchctl enable "$GUI/$LABEL"
launchctl kickstart -k "$GUI/$LABEL"

echo "==> Installed. Follow logs with:"
echo "    tail -f \"$LOG_DIR/agentpulsed.err.log\""

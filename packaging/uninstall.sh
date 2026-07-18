#!/usr/bin/env bash
# Unload the LaunchAgent and remove installed files.
set -euo pipefail

LABEL="com.aamorifreeman.agentpulse"
PLIST_DEST="$HOME/Library/LaunchAgents/$LABEL.plist"
PROGRAM="$HOME/.local/bin/agentpulsed"
GUI="gui/$(id -u)"

echo "==> Unloading LaunchAgent"
launchctl bootout "$GUI/$LABEL" 2>/dev/null || true

echo "==> Removing files"
rm -f "$PLIST_DEST" "$PROGRAM"

echo "==> Uninstalled. Data and logs left in place:"
echo "    ~/Library/Application Support/AgentPulse"
echo "    ~/Library/Logs/AgentPulse"

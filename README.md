# AgentPulse

A lightweight **macOS system & automation reliability monitor**. A C++20 daemon
schedules and monitors local automations (Python/shell scripts, launchd-style jobs)
*and* samples system health (CPU, memory, disk, thermal), correlates the two — e.g.
"this job was missed because the Mac was asleep" — and surfaces actionable alerts
through a native menu-bar app.

See [`SCOPE.md`](SCOPE.md) for architecture, decisions, and milestones.

## Status

Early development. Milestone progress:

- [x] **M0** — Project setup & daemon skeleton (CMake, C++20, SQLite, launchd)
- [ ] **M1** — Vertical slice: CPU metric end-to-end (daemon → socket → app)
- [ ] **M2** — Automation core: scheduler & process supervisor
- [ ] **M3** — System health collectors (memory, disk, thermal)
- [ ] **M4** — Alert-rule engine & native notifications
- [ ] **M5** — Missed-run detection & wake recovery
- [ ] **M6** — Polish, charts & overhead instrumentation

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (Apple clang). SQLite ships with macOS.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/daemon/agentpulsed --version
```

## Install as a LaunchAgent

Builds, installs `agentpulsed` to `~/.local/bin`, and loads it as a per-user
launchd agent (starts at login, restarts on unexpected exit):

```sh
./packaging/install.sh
tail -f ~/Library/Logs/AgentPulse/agentpulsed.err.log
```

To remove:

```sh
./packaging/uninstall.sh
```

## Layout

```
daemon/         C++20 daemon (agentpulsed)
  src/          sources
packaging/      LaunchAgent plist template + install/uninstall scripts
SCOPE.md        full scope & milestone plan
```

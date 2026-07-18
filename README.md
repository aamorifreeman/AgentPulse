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
- [x] **M1** — Vertical slice: CPU metric end-to-end (daemon → socket → `apctl`)
- [x] **M2** — Automation core: scheduler & process supervisor
- [x] **M3** — System health collectors (memory, disk, thermal)
- [x] **M4** — Alert-rule engine & native notifications
- [x] **M5** — Missed-run detection & wake recovery
- [x] **M6** — Polish, charts & overhead instrumentation

The daemon is feature-complete. The native menu-bar app (SwiftUI) builds on
the now-stable socket API (see [`docs/PROTOCOL.md`](docs/PROTOCOL.md)); `apctl`
is the reference client in the meantime.

## Architecture

```
┌───────────────────────────────┐
│   apctl / (SwiftUI menu-bar)   │   status · jobs · alerts · history · run
└───────────────┬───────────────┘
                │  Unix domain socket, newline-delimited JSON
┌───────────────▼───────────────┐
│      agentpulsed  (C++20)      │
│  metric collectors · scheduler │
│  process supervisor · alert    │
│  rule engine · sleep monitor   │
└───────────────┬───────────────┘
                │
┌───────────────▼───────────────┐
│            SQLite (WAL)        │   metrics · runs · alerts
└───────────────────────────────┘
```

- **Collectors** (`metrics/`): CPU (Mach `host_statistics`), memory
  (`vm_statistics64`), disk (`statfs`), thermal (`ProcessInfo.thermalState`),
  per-process CPU/RSS (`libproc`), and the daemon's own overhead.
- **Scheduler** (`jobs/`): cron scheduling, `posix_spawn` supervision with
  stdout/stderr/exit-code/timeout capture and retries, missed-run detection.
- **Alert engine** (`alerts/`): duration/cooldown/recovery/severity, process
  attribution, quiet hours — pure and unit-tested.
- **Power** (`power/`): IOKit sleep/wake for wake recovery.
- **IPC** (`ipc/`): Unix-domain-socket server + JSON API.

## Measured overhead

Self-instrumented (`daemon.rss_bytes` / `daemon.cpu.percent`), 5s sampling:

- **RSS:** ~7.3 MB resident.
- **CPU:** ~0.4% average — a brief spike each 5s sample tick, effectively idle
  between ticks.

## Build

Requires CMake ≥ 3.20 and a C++20 compiler (Apple clang). SQLite ships with
macOS. JSON uses [nlohmann/json]; the build prefers a local/Homebrew copy and
falls back to fetching it:

```sh
brew install cmake nlohmann-json yaml-cpp   # deps (avoids configure-time fetch)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Binaries: `build/daemon/agentpulsed` (daemon) and `build/cli/apctl` (client).

## Run

Optionally register automations by copying [`examples/config.yaml`](examples/config.yaml)
to `~/.config/agentpulse/config.yaml`, then:

```sh
./build/daemon/agentpulsed &          # samples system + runs scheduled jobs
./build/cli/apctl status              # CPU + job health
./build/cli/apctl jobs                # configured jobs, next/last run
./build/cli/apctl run email-scan      # trigger a job now
./build/cli/apctl --watch 2           # live view
```

[nlohmann/json]: https://github.com/nlohmann/json

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
daemon/         C++20 daemon (agentpulsed) + agentpulse_core static lib
  src/          sources (metrics/, ipc/, db, paths, log)
cli/            apctl — reference socket client
tests/          dependency-free test harness (ctest)
packaging/      LaunchAgent plist template + install/uninstall scripts
docs/           PROTOCOL.md — socket API
SCOPE.md        full scope & milestone plan
```

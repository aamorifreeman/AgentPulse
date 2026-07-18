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
- [ ] **M4** — Alert-rule engine & native notifications
- [ ] **M5** — Missed-run detection & wake recovery
- [ ] **M6** — Polish, charts & overhead instrumentation

> The native menu-bar app (SwiftUI) is intentionally built after the socket
> API stabilizes (post-M3), so the GUI isn't chasing a moving contract. Until
> then, `apctl` is the reference client. See [`docs/PROTOCOL.md`](docs/PROTOCOL.md).

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

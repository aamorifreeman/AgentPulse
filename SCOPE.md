# AgentPulse — Scope

**macOS system & automation reliability monitor.** A lightweight C++20 daemon that
schedules/monitors local automations *and* samples system health, correlates the two
(e.g. "job missed because the Mac was asleep"), and surfaces actionable alerts through a
native menu-bar app.

## Goals

- **Real tool:** something the author actually runs to keep personal automations honest.
- **Showpiece:** demonstrates modern C++, OS APIs, concurrency, IPC, time-series storage,
  and an alerting/observability layer, fronted by a polished native macOS UI.

Both halves — automation monitoring and system health — ship in the MVP. We de-risk by
building a thin end-to-end slice first, then widening.

## Architecture

```
┌───────────────────────────────┐
│      Menu-bar app (SwiftUI)   │   Dashboard · Alerts · Run/Retry/Stop
└───────────────┬───────────────┘
                │  Unix domain socket, newline-delimited JSON
┌───────────────▼───────────────┐
│      AgentPulse daemon (C++20) │
│  metric collectors · scheduler │
│  process supervisor · alert    │
│  rule engine · event/log store │
└───────────────┬───────────────┘
                │
┌───────────────▼───────────────┐
│            SQLite              │   metrics · runs · logs · alerts
└───────────────────────────────┘
```

## Locked technical decisions

| Area | Decision | Why |
|------|----------|-----|
| Language / build | C++20, CMake | Résumé target; modern C++ idioms |
| Daemon lifecycle | launchd **LaunchAgent** (per-user) | Runs in GUI session (needed for notifications); ties into launchd story |
| IPC | **Unix domain socket** + newline-delimited JSON | No open TCP port; file-perms security; shows real IPC. Fallback: loopback HTTP+token if Swift UDS client proves painful |
| CPU metrics | `host_statistics64` (delta of tick counts between samples) | Official Mach API |
| Memory | `host_statistics64` (`vm_statistics64`) + memory-pressure notifications | — |
| Disk | `statfs` | — |
| Thermal | `ProcessInfo.thermalState` (nominal/fair/serious/critical) via Objective-C++ (`.mm`) | Stable, documented signal — *not* a fake exact temperature. `powermetrics` is an optional later collector |
| Sleep/wake | IOKit `IORegisterForSystemPower` | Clean event source for missed-run detection |
| Storage | SQLite (single file) | Time-series + run/log history; zero-config |
| Config | YAML: `jobs:` and `rules:` | Human-editable; matches the spec examples |
| Notifications | `UserNotifications` from the Swift app | Native, actionable ("Run now?") |

## Config shape (target)

```yaml
jobs:
  - name: email-scan
    command: python3 email_scan.py
    schedule: "0 8 * * *"
    missed_run_policy: run_on_wake   # none | run_on_wake | run_now
    timeout_seconds: 300
    retries: 2

rules:
  - name: sustained-high-cpu
    metric: system.cpu.percent
    condition: greater_than
    threshold: 90
    duration_seconds: 300
    cooldown_seconds: 1800
  - name: thermal-pressure
    metric: system.thermal_state
    condition: at_least
    threshold: serious
    cooldown_seconds: 900
  - name: low-disk-space
    metric: disk.available_gb
    condition: less_than
    threshold: 10
```

## Alert engine requirements (the differentiator)

- **Duration** gates (X above threshold for N seconds), **cooldown** (no repeat for M
  seconds), **recovery** notices, **severity** (warning/serious/critical),
  **process attribution** (top offender), **compound rules** (high CPU *and* serious
  thermal), **quiet hours** (hold non-critical until morning).

## Milestones

Each milestone is independently demoable.

- **M0 — Setup.** `brew install cmake`; CMake skeleton, C++20, links SQLite; empty daemon
  starts/stops cleanly; installs as a LaunchAgent.
- **M1 — Vertical slice (de-risk the seam).** Daemon samples CPU% on a timer, writes to
  SQLite, and serves it over the Unix socket. `apctl` (CLI reference client) connects and
  shows live CPU. *End-to-end pipe proven.* **The SwiftUI menu-bar app is deferred until
  the socket API stabilizes (post-M3)** so the GUI isn't built against a moving contract;
  `apctl` is the client until then.
- **M2 — Automation core.** Job scheduler (cron), process supervisor: launch, capture
  stdout/stderr, exit code, duration → SQLite. App lists jobs + last run + "Run now".
- **M3 — System health.** Add memory, disk, thermal collectors. App shows system panel +
  top processes.
- **M4 — Alert engine.** Rule evaluation with duration/cooldown/recovery/severity;
  native notifications from the app.
- **M5 — Missed-run + wake recovery.** IOKit sleep/wake tracking; detect schedules skipped
  while asleep; apply `missed_run_policy`. Retries with backoff.
- **M6 — Polish.** History charts, quiet hours, compound rules, config hot-reload.

Stretch: network/throughput, battery health, `powermetrics` collector, file-sync checks
(git unpushed commits, "expected file didn't update").

## Instrument for the résumé

Capture from day one so the bullet points have real numbers:

- Daemon **idle CPU %** and **RSS (MB)** over a 24h run.
- Sample/eval loop latency; socket round-trip time.
- Concurrent jobs handled without missed samples.

## Non-goals (MVP)

- Exact temperature readings. Multi-user/system-wide daemon (per-user only). Remote/cloud
  sync. Windows/Linux.

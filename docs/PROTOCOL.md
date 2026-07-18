# AgentPulse socket protocol

The daemon (`agentpulsed`) listens on a Unix-domain stream socket at:

```
~/Library/Application Support/AgentPulse/agentpulse.sock
```

The socket is created with mode `0600` (owner-only).

## Framing

**One request per connection.** A client connects, writes a single request
line terminated by `\n`, reads a single response line terminated by `\n`, and
the connection closes. This keeps the model trivial and suits a menu-bar app
that polls periodically.

## Requests

A request is either:

- a JSON object with a `cmd` field — `{"cmd":"status"}`, or
- a bare command word — `status` (convenient for `nc -U` / manual testing).

### Commands

| Command             | Description                                    |
|---------------------|------------------------------------------------|
| `ping`              | Liveness check.                                |
| `status`            | Current snapshot: daemon overhead + CPU + system + jobs + alerts. |
| `jobs`              | List configured jobs with next run + last run. |
| `alerts`            | Recent alert transitions (firing/recovered).   |
| `history <metric> [seconds]` | Metric samples over a recent window (default 3600s). |
| `runs <job> [limit]`| Recent runs for a job (default 20).            |
| `run <name>`        | Trigger an immediate run of a job.             |

`run` takes a job name, as JSON `{"cmd":"run","job":"email-scan"}` or the bare
form `run email-scan`. `history`/`runs` accept a bare second argument
(`history system.cpu.percent 600`) or JSON (`{"cmd":"history",
"metric":"system.cpu.percent","seconds":600}`).

`run` takes a job name, as JSON `{"cmd":"run","job":"email-scan"}` or the bare
form `run email-scan`.

## Responses

A response is a JSON object. Success carries `"ok": true`; errors carry
`"ok": false` and an `"error"` string.

`ping`:

```json
{"ok": true, "cmd": "ping", "reply": "pong"}
```

`status` (CPU + system health + jobs):

```json
{
  "ok": true,
  "cmd": "status",
  "daemon": { "started_at": 1752800000, "rss_bytes": 7654400, "cpu_percent": 0.4 },
  "cpu": { "valid": true, "percent": 12.3, "sampled_at": 1752800012 },
  "system": {
    "valid": true,
    "sampled_at": 1752800012,
    "memory": { "total_bytes": 25769803776, "used_bytes": 12884901888, "used_percent": 50.0 },
    "disk": { "total_bytes": 494384795648, "available_bytes": 237000000000, "used_percent": 52.0 },
    "thermal_state": "nominal",
    "top_processes": [
      { "pid": 431, "name": "Google Chrome", "cpu_percent": 184.2, "rss_bytes": 1610612736 }
    ]
  },
  "jobs": [
    {
      "name": "email-scan",
      "schedule": "0 8 * * *",
      "next_run": 1752825600,
      "running": false,
      "last_run": {
        "status": "success",
        "started_at": 1752739200,
        "exit_code": 0,
        "duration_ms": 812,
        "trigger": "schedule"
      }
    }
  ]
}
```

`jobs` returns `{"ok": true, "cmd": "jobs", "jobs": [ ... ]}` with the same job
objects. `run` returns `{"ok": true, "cmd": "run", "job": "email-scan",
"queued": true}` or `{"ok": false, "error": "no such job", "job": "..."}`.

`alerts` returns recent transitions:

```json
{
  "ok": true,
  "cmd": "alerts",
  "alerts": [
    {
      "ts": 1752800300,
      "rule": "sustained-high-cpu",
      "severity": "serious",
      "metric": "system.cpu.percent",
      "kind": "firing",
      "value": 96.4,
      "threshold": 90.0,
      "message": "sustained-high-cpu: system.cpu.percent = 96.4 (greater_than 90)",
      "attribution": "Google Chrome — 184% CPU",
      "notified": true
    }
  ]
}
```

Unknown command:

```json
{"ok": false, "error": "unknown command", "cmd": "bogus"}
```

`history` returns `{"ok": true, "cmd": "history", "metric": "...", "points":
[{"t": 1752800000, "v": 12.3}, ...]}` (oldest first). `runs` returns `{"ok":
true, "cmd": "runs", "job": "...", "runs": [{"started_at": ..., "status":
"success", "exit_code": 0, "duration_ms": 812, "trigger": "schedule"}, ...]}`.

## Client

`apctl` is the reference client:

```sh
apctl ping
apctl status
apctl jobs
apctl alerts
apctl history system.cpu.percent 600
apctl runs email-scan 10
apctl run email-scan
apctl --watch 2        # refresh status every 2s
apctl --socket /path/to/agentpulse.sock status
```

Reload config without restarting (rules & quiet hours; job changes need a
restart):

```sh
kill -HUP $(pgrep agentpulsed)
```

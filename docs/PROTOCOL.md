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

| Command       | Description                                    |
|---------------|------------------------------------------------|
| `ping`        | Liveness check.                                |
| `status`      | Current daemon snapshot: CPU + jobs.           |
| `jobs`        | List configured jobs with next run + last run. |
| `run <name>`  | Trigger an immediate run of a job.             |

`run` takes a job name, as JSON `{"cmd":"run","job":"email-scan"}` or the bare
form `run email-scan`.

## Responses

A response is a JSON object. Success carries `"ok": true`; errors carry
`"ok": false` and an `"error"` string.

`ping`:

```json
{"ok": true, "cmd": "ping", "reply": "pong"}
```

`status` (M1 — CPU only; later milestones extend this):

```json
{
  "ok": true,
  "cmd": "status",
  "daemon": { "started_at": 1752800000 },
  "cpu": { "valid": true, "percent": 12.3, "sampled_at": 1752800012 },
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

Unknown command:

```json
{"ok": false, "error": "unknown command", "cmd": "bogus"}
```

## Client

`apctl` is the reference client:

```sh
apctl ping
apctl status
apctl --watch 2        # refresh status every 2s
apctl --socket /path/to/agentpulse.sock status
```

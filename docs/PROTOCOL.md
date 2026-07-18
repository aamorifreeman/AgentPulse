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

| Command  | Description                          |
|----------|--------------------------------------|
| `ping`   | Liveness check.                      |
| `status` | Current daemon + system snapshot.    |

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
  "cpu": { "valid": true, "percent": 12.3, "sampled_at": 1752800012 }
}
```

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

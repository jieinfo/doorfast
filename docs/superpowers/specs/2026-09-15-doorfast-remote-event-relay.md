# Doorfast Remote Event Relay Design

## Problem
The daemon's event stream is a local Unix socket. Home Assistant normally runs on another host and cannot open `/var/run/doorfast/events.sock`. The HA integration therefore exposes an authenticated HTTP ingress at `/api/doorfast/<entry_id>`, while Doorfast needs a separate relay.

## Decision
Implement the relay as a small Doorfast-side procd service. It reads newline-delimited JSON from the local socket, validates the fixed event schema, and sends HTTPS POST requests to the configured HA ingress. The relay is optional; status polling remains the authoritative fallback.

The relay must not shell out with a bearer token in argv. It should use an HTTP client library or a dedicated C transport that keeps the token in memory and validates the configured CA. `uclient-fetch` and shell `socat` are unsuitable as the production implementation because of credential exposure and weak delivery semantics.

## Configuration
Add a separate UCI config section, disabled by default:

- `enabled`: 0/1
- `url`: full `https://host/api/doorfast/<entry_id>` URL
- `token_file`: root-owned file containing the HA bearer token
- `ca_file`: required CA bundle/path for HTTPS verification
- `connect_timeout_ms`, `request_timeout_ms`
- `queue_capacity`: bounded, default 64
- `backoff_min_ms`, `backoff_max_ms`

The token file must be mode 0600 and owned by root. The relay refuses to start when the URL is not HTTPS, the token file is too permissive, or CA verification cannot be configured.

## Delivery and failure behavior
The relay acknowledges a local line only after a 2xx response. It keeps at most `queue_capacity` events in memory, drops the oldest event when full, and logs a metric. Failed requests reconnect with exponential backoff and jitter bounded by the configured maximum. A daemon restart may lose queued events; HA polling repairs state. Duplicate and stale suppression remains in HA.

The relay sends the original `schema_version`, `event_id`, `event`, `generation`, and `timestamp_ms` fields without rewriting generation. It never blocks the Doorfast call loop because it is a separate process.

## Verification
- unit tests for schema validation, queue bound, retry/backoff, token-file checks, and HTTPS certificate failure
- OpenWrt package manifest and procd lifecycle tests
- isolated VM test with a local HTTPS test receiver, forced disconnects, duplicate events, queue saturation, and daemon restart
- verify existing network/firewall configuration hashes remain unchanged

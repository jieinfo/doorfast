# Doorfast Remote HA Event Relay Design

## Decision

The local event socket is a valid producer boundary, but it cannot be consumed directly by a Home Assistant instance on another host. The current HA endpoint is an authenticated HTTP POST view, so remote active events require a Doorfast-side relay.

Do not implement the relay with `socat | shell | uclient-fetch` in the first release. The VM has `uclient-fetch`, OpenSSL transport, and `socat`, but that composition has three material problems: bearer credentials appear in process arguments, a shell pipeline has no durable per-event acknowledgement or bounded retry queue, and reconnect/backpressure behavior is difficult to test without making event loss ambiguous.

Do not add a raw TLS client to the main daemon. It would enlarge the call-loop failure surface and duplicate TLS/HTTP implementation already supplied by OpenWrt.

## Recommended boundary

Add a separate `doorfast-event-relay` procd service in a later task. It should read `/var/run/doorfast/events.sock` as a member of the `doorfast` group and deliver one JSON object per POST to the configured Home Assistant endpoint. The main Doorfast daemon remains independent: if the relay is absent or stopped, calls and the local event publisher continue normally.

The relay must use an OpenWrt HTTP client library or a small dedicated executable that can keep the bearer token out of argv and provide explicit response handling. Before implementation, verify which target package/API is available in the supported ImmortalWrt feeds. `uclient-fetch` is available in the current VM and supports HTTPS, CA files, headers, and POST bodies, but its CLI form exposes header values in argv; this is not accepted as the production credential path without an OS-level mitigation.

## UCI configuration

Use a separate `/etc/config/doorfast-events` file, mode `0600`, with one `relay` section:

```
config relay 'main'
        option enabled '0'
        option url ''
        option entry_id ''
        option token_file '/etc/doorfast/ha-token'
        option ca_file '/etc/ssl/certs/ca-certificates.crt'
        option connect_timeout_ms '3000'
        option request_timeout_ms '5000'
        option retry_max '3'
```

The relay must reject startup when `enabled=1` and URL, entry ID, token file, or CA validation is missing. HTTPS is mandatory; an explicit loopback-only HTTP mode could be considered for development tests but must not be a production fallback.

The final URL is `url` plus `/api/doorfast/` plus a validated entry ID. The token is read from a root-owned `0600` file and supplied through the chosen HTTP library's in-memory request header API. Do not put the token in UCI, command-line arguments, logs, or event payloads.

## Delivery semantics

- Connect to the local stream with bounded exponential backoff: 250 ms, 500 ms, 1 s, 2 s, then 5 s maximum.
- Read complete JSON Lines only; cap a line at 4 KiB and reject malformed or oversized lines.
- Keep at most 64 pending events. On overflow, drop the oldest event and record a rate-limited diagnostic; HA polling remains authoritative.
- POST with `Content-Type: application/json` and `Authorization: Bearer <token>`.
- Treat 2xx as delivered. Retry 408, 425, 429, and 5xx with bounded backoff; do not retry other 4xx responses until configuration changes.
- Preserve `event_id` and `generation`; the HA endpoint deduplicates `(generation,event)` and refreshes authoritative status after accepted delivery.
- On daemon or relay restart, reconnect and rely on the existing polling path for events missed during downtime.

## Threat model

The local socket is protected by `root:doorfast`, mode `0660`; only the relay account/group may read it. The relay token is a bearer credential and must be protected as a root-owned `0600` file. TLS certificate validation is required. The relay must not accept inbound network connections, expose the Unix socket, or log request bodies or Authorization headers. A compromised relay host can still forge events; HA must refresh status and ignore stale generations.

## Acceptance tests

1. Unit: URL/entry ID validation, token file permission checks, line-size cap, JSON validation, status-class retry policy, bounded queue, and monotonic backoff.
2. Integration: fake HTTPS server with a test CA verifies bearer header, exact JSON body, 2xx acknowledgement, retryable failures, permanent 4xx stop, and reconnect after socket EOF.
3. VM: install relay package with a test CA/token; verify socket owner/mode, no token in `ps`/syslog, event delivery, bounded retry, daemon restart, and unchanged Doorfast control-plane/firewall hashes.
4. HA: authenticated endpoint accepts a valid event, rejects missing/invalid auth, suppresses duplicates, rejects stale generations, and keeps polling fallback when relay is stopped.

## Work split

- R1: select and verify a target HTTP client API available in supported ImmortalWrt feeds; document package/version constraints.
- R2: implement the isolated relay process and UCI/init lifecycle.
- R3: add HA configuration for the relay endpoint/token and event consumer tests.
- R4: run VM acceptance and update capability documentation.

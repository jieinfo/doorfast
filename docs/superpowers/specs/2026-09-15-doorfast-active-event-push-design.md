# Doorfast Active Event Push Design

## Goal

Deliver authenticated, local, generation-bound Doorfast events to Home Assistant without making polling the only way to discover a call.

## Scope

The first release covers `incoming_call`, `call_established`, `hangup`, `timeout`, and `preempted`. It does not carry audio, video, credentials, or arbitrary commands. Polling remains the recovery path.

## Architecture

Doorfast writes newline-delimited JSON events to a root-owned Unix stream socket at `/var/run/doorfast/events.sock`. The socket is created with mode `0660`, owned by `root:doorfast`, and the HA bridge runs as a member of the local `doorfast` group. Each client receives a bounded sequence of events; a slow or disconnected client is dropped without blocking the call loop.

Every event contains `schema_version`, `event_id`, `event`, `generation`, and `timestamp_ms`. `event_id` is monotonic per daemon start and generation is the existing call generation. A client deduplicates `(generation, event)` and ignores events for an older generation. The event stream is advisory: HA refreshes `/api/v1/status` after each event and continues its existing five-second poll.

## Security and failure handling

The socket is local-only and never exposed by the HTTP CGI. Invalid JSON, oversized lines, malformed generations, and unknown event names are rejected by the consumer. The producer keeps at most 64 events per client and drops the client on backpressure. A daemon restart resets the event sequence; HA treats a reconnect as a reason to refresh status.

## Compatibility

Existing ubus, HTTP, video, audio, and PCM interfaces are unchanged. If the socket is absent, Doorfast runs normally and HA uses polling. Older HA installations continue to work without the event stream.

## Verification

Host tests cover event serialization, bounded buffering, generation filtering, reconnect, and producer non-blocking behavior. HA tests cover event parsing, duplicate suppression, stale-generation rejection, reconnect refresh, and polling fallback. VM acceptance verifies socket ownership, mode, event delivery, daemon restart, and unchanged control-plane ACLs.

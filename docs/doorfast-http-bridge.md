# Doorfast HTTP bridge smoke test

The package installs `/www/cgi-bin/doorfast`. With uhttpd serving CGI scripts,
the read-only status endpoint is:

```text
http://<doorfast-host>/cgi-bin/doorfast/api/v1/status
```

After installing the package, run the smoke test from a machine that can reach
the host:

```text
python3 tests/run_doorfast_http_status.py http://<doorfast-host>/cgi-bin/doorfast
```

The script only calls `status`; it does not unlock, answer, hang up, or call an
elevator. Test those actions from Home Assistant after confirming the device is
in active host mode and the local network is isolated.

## Submit microphone PCM

Package release `0.1.0-r39` adds a bounded HTTP producer in
`/usr/sbin/doorfast-pcm-http`. The CGI dispatches the following three routes to
that helper before reading the request body, so binary PCM is never stored in a
shell variable.

First read `/api/v1/status`. Start only while `call.session` is `talking`,
`audio_tx.active` is true, and `audio_tx.generation` equals the nonzero
`call.generation`. Save the 16-character lowercase hexadecimal `runtime_id` and
the generation from that response.

Open the producer session:

```http
POST /cgi-bin/doorfast/api/v1/audio/session?runtime=<runtime_id>&generation=<generation>
Content-Length: 0
```

A successful response contains a random 32-character lowercase hexadecimal
`audio_session`, the authoritative `next_sequence`, and `lease_ms: 2000`:

```json
{
  "status": "success",
  "runtime_id": "0123456789abcdef",
  "generation": 42,
  "audio_session": "0123456789abcdef0123456789abcdef",
  "accepted_frames": 0,
  "next_sequence": 0,
  "lease_ms": 2000
}
```

Submit one to five consecutive PCM frames:

```http
POST /cgi-bin/doorfast/api/v1/audio/submit.pcm?runtime=<runtime_id>&generation=<generation>&sequence=<next_sequence>
X-Doorfast-Audio-Session: <audio_session>
Content-Type: application/octet-stream
Content-Length: <320|640|960|1280|1600>

<raw PCM frames>
```

Each 320-byte frame is exactly 160 signed 16-bit little-endian samples: 20 ms
of mono 8 kHz audio. The five legal body sizes therefore contain one through
five frames. `generation` and `sequence` are unsigned 64-bit decimal values;
the sequence plus the frame count must not overflow that range. The helper
reads the complete bounded body and rejects short input or trailing bytes
before sending its first frame. It sends the first frame immediately and paces
later frames no closer than their monotonic 20 ms deadlines.

After each successful local datagram send, the helper increments the response's
`accepted_frames` and `next_sequence`, then tries to write the sequence and
renewed lease to its current state record. A normal `200` batch response means
every increment was written to that record and the two-second producer lease
was renewed:

```json
{
  "status": "success",
  "runtime_id": "0123456789abcdef",
  "generation": 42,
  "audio_session": "",
  "accepted_frames": 5,
  "next_sequence": 5,
  "lease_ms": 2000
}
```

Release the lease when capture stops:

```http
POST /cgi-bin/doorfast/api/v1/audio/session/end?runtime=<runtime_id>&generation=<generation>
X-Doorfast-Audio-Session: <audio_session>
Content-Length: 0
```

Only one unexpired producer token can own a runtime and generation. A second
session open returns `409 producer_busy`. After release or two seconds without
an accepted frame, a new producer can open a session and continues from the
stored `next_sequence`. Clients must keep at most one request in flight. The
helper serializes state transitions, but parallel client requests would add
delayed speech and make capture recovery ambiguous.

## Recover a producer

Use this state machine in a browser or Home Assistant producer:

1. Read fresh status and wait for an active talking generation.
2. Open a session and set the local sequence to the returned `next_sequence`.
3. Capture at most five current frames and submit one request. Keep only one
   request in flight; while waiting, buffer no more than the next bounded batch.
4. On `200`, discard the accepted batch and continue at `next_sequence`.
5. On any failure or timeout, pause capture immediately and discard audio
   recorded after the failed request. Read fresh status before recovery.
6. If `runtime_id` or generation changed, end the capture session and discard every buffered frame. Never submit old-call audio to the new generation.
7. Never replay the `accepted_frames` prefix reported by a failed response,
   even when persistence failed. Discard the entire old request body before
   recovery; use only newly captured audio afterward.
8. Reconcile sequence state with the server. A successful session open or
   batch, and `sequence_duplicate` or `sequence_gap` for the matching runtime
   and generation, provides the sequence to use next. If the current token is
   still valid, submit newly captured audio at the candidate sequence; a
   duplicate or gap rejection sends no audio and supplies the stored sequence.
   After `session_expired`, open a new session and use its returned sequence.
9. A deliberate stop should call `session/end` on a best-effort basis.

A `503` can report a successfully submitted prefix. For example,
`accepted_frames: 2` means those two PCM payloads must not be replayed. Most
such responses also report the advanced stored sequence, but
`state_unavailable` can occur after a datagram send and before that increment is
persisted. Its reported `next_sequence: 7` therefore records send progress, not
a guaranteed durable cursor. Reopen the session when possible, or let a
`sequence_duplicate`/`sequence_gap` response reconcile the stored cursor, and
continue with newly captured audio only.

If an HTTP response is lost, the client does not know how much of its body was
submitted. It must discard that body rather than retry it. After reading fresh
status, it uses its next candidate sequence with newly captured audio; a
duplicate or gap rejection reconciles the cursor without replaying the
ambiguous recording.

All responses use JSON and `Cache-Control: no-store`. A successful response
contains `status`, `runtime_id`, `generation`, `audio_session`,
`accepted_frames`, `next_sequence`, and `lease_ms`. When authoritative status
was available, an error also contains `runtime_id`, `generation`,
`accepted_frames`, and `next_sequence`.

| HTTP status | Error values and meaning |
| --- | --- |
| `400 Bad Request` | `invalid_request` or `invalid_body`: malformed, missing, duplicated or unsupported route query fields; invalid required session token or content metadata; invalid length, body, or numeric range |
| `404 Not Found` | `not_found`: the helper was invoked for an unknown PCM route |
| `405 Method Not Allowed` | `method_not_allowed`: these three routes accept only `POST` |
| `409 Conflict` | `runtime_mismatch`, `generation_mismatch`, `call_not_talking`, `audio_tx_inactive`, `producer_busy`, `session_mismatch`, `session_expired`, `sequence_duplicate`, or `sequence_gap` |
| `500 Internal Server Error` | `invalid_config`: helper configuration or acceptance invocation is invalid |
| `503 Service Unavailable` | `status_unavailable`, `state_unavailable`, `random_unavailable`, `pacing_unavailable`, `send_unavailable`, or `clock_unavailable`; never replay the returned accepted prefix, and reconcile the stored sequence as described above |

The producer token coordinates accidental concurrent producers; it is not HTTP
authentication. Restrict this CGI to a trusted HA/router network or place it
behind authenticated HTTPS. Browser microphone capture also requires HTTPS or
another browser-recognized secure context.

HTTP `200` proves that the frames entered Doorfast's local Unix datagram
ingress. The daemon still rechecks call state, generation, pacing and its
learned GVS audio route before UDP/8302 transmission. HTTP success does not
prove MT8157 speaker format compatibility, intelligibility, end-to-end latency,
echo behavior, or long-call stability; those require physical-device tests.

The status response includes a `video` table:

| Field | Meaning |
| --- | --- |
| `ready` | A complete validated JPEG exists for the current call generation |
| `generation` | Call generation that produced the frame; zero when unavailable |
| `frame_no` | Latest 16-bit GVS video frame number |
| `bytes` | Complete JPEG size |
| `timestamp_ms` | Monotonic receive time on the Doorfast host |

When `video.ready` is true, the matching snapshot is available at
`/api/v1/video/latest.jpg`. Startup, preemption, hangup, timeout, network loss,
and snapshot publication failure set the status back to unavailable and remove
the file. Consumers must compare `video.generation` with `call.generation`
instead of treating an earlier image as current.

Consumers can bind the image request to the active call by appending the
generation returned by `status`:

```text
/api/v1/video/latest.jpg?generation=<call.generation>
```

The bridge returns `409 Conflict` if the requested generation is no longer
current. Successful responses include `X-Doorfast-Generation`,
`X-Doorfast-Frame`, and an `ETag` derived from both values. Send that ETag in
`If-None-Match` to receive `304 Not Modified` while the latest complete frame
has not changed. The bridge copies the atomically published snapshot and checks
its status again before responding; a concurrent frame transition returns a
short-lived `503 Service Unavailable` response with `Retry-After: 1`.

The latest decoded audio window follows the same contract at
`/api/v1/audio/latest.wav?generation=<call.generation>`. The `audio` status
table distinguishes received buffered data from a successfully published WAV
using `snapshot_ready`, `snapshot_packet_count`, `snapshot_bytes`, and
`snapshot_timestamp_ms`. Audio responses use `snapshot_packet_count` as their
revision in `ETag` and `X-Doorfast-Audio-Revision`.

Each WAV contains only samples accepted since the previous successful export;
it does not repeat the complete rolling receive buffer. The runtime keeps four
immutable chunks, normally covering about four seconds, and removes the whole
queue at every call lifecycle boundary. A sequential consumer can send
`after=<snapshot_packet_count>` with the generation. The current revision
returns `304 Not Modified`; any retained cursor returns its next WAV chunk; an
expired or unknown cursor returns `409 Conflict` with the latest revisions for
resynchronization. `snapshot_previous_packet_count` and
`snapshot_dropped_bytes` are also exposed in status. A nonzero dropped byte
count means the producer received more unexported audio than the bounded ring
could retain.

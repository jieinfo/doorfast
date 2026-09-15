# Doorfast HTTP PCM Bridge Design

## Purpose

Doorfast already accepts one 20 ms, 8 kHz, mono, signed 16-bit little-endian PCM frame through the private `/var/run/doorfast-audio.sock` Unix datagram socket. The missing layer is a network-facing producer that a browser or Home Assistant client can use without gaining access to the router filesystem.

This phase adds a bounded HTTP-to-PCM bridge. It does not add a browser microphone interface yet. A later browser task will use this interface after establishing a secure browser context and performing capture and resampling.

## Scope

The first implementation will:

- add session-open, PCM-submit and session-end routes under `/api/v1/audio` to the existing Doorfast HTTP bridge;
- accept short batches containing one to five existing PCM frames;
- bind every request to the current daemon runtime, call generation and a continuous frame sequence;
- issue a short-lived, unguessable producer session so only one client can submit a generation at a time;
- submit frames to the existing Unix socket at 20 ms intervals;
- make interrupted, duplicated, stale and partially accepted requests recoverable without replaying accepted audio;
- package and test the helper on ImmortalWrt.

The implementation will not add WebRTC, echo cancellation, automatic gain control, silence generation, a persistent audio recording, transcoding from compressed browser formats, or a Home Assistant/LuCI microphone control. It will not claim speaker-side interoperability until physical hardware confirms the vendor audio assumptions.

## Alternatives considered

Sending one HTTP request per 20 ms frame would be simple, but it would create roughly 50 CGI processes and requests per second. Browser scheduling and HTTP overhead would also make that path fragile on a small router.

A new WebRTC service would provide better interactive media transport, but it would add signaling, ICE, TLS and codec dependencies before the raw GVS audio path has physical-device evidence.

The selected design sends at most five frames per request. A 100 ms upper bound keeps latency and recovery bounded while reducing request frequency to about ten requests per second. The existing Doorfast transmitter remains the final authority for generation, call state, pacing and G.711 A-law encoding.

## HTTP contract

A producer first opens a session:

```text
POST /cgi-bin/doorfast/api/v1/audio/session?runtime=<r>&generation=<g>
Content-Length: 0
```

`runtime` is the current 16-character hexadecimal `runtime_id` exposed in Doorfast status. The daemon creates a new random `runtime_id` at every start, so a delayed request from an earlier daemon instance cannot become valid when a numeric call generation is reused. `generation` is the current nonzero call generation.

If no producer lease is active, the response returns a cryptographically random 128-bit audio session token, the current `next_sequence`, and the lease duration. A valid batch renews the two-second lease. A second client receives `409 Conflict` while the lease remains active. After expiry, a new session token may claim the producer role and resumes at the stored next sequence. The token is returned in JSON but is subsequently carried in the `X-Doorfast-Audio-Session` header so it does not enter access-log URLs.

```json
{
  "status": "success",
  "runtime_id": "0123456789abcdef",
  "generation": 42,
  "audio_session": "0123456789abcdef0123456789abcdef",
  "next_sequence": 100,
  "lease_ms": 2000
}
```

Audio batches use:

```text
POST /cgi-bin/doorfast/api/v1/audio/submit.pcm?runtime=<r>&generation=<g>&sequence=<s>
X-Doorfast-Audio-Session: <token>
Content-Type: application/octet-stream
Content-Length: <320, 640, 960, 1280, or 1600>
```

`sequence` is a zero-based frame sequence within the current runtime and generation. Generation and sequence values must be unsigned 64-bit decimal integers, the runtime ID and session token must use their exact lowercase hexadecimal forms, and the batch must not overflow the sequence range. The body consists only of consecutive 320-byte PCM frames. Each frame represents 160 samples and 20 ms.

A producer may release its lease without waiting for expiry:

```text
POST /cgi-bin/doorfast/api/v1/audio/session/end?runtime=<r>&generation=<g>
X-Doorfast-Audio-Session: <token>
Content-Length: 0
```

All three routes reject methods other than `POST` and missing or malformed query values or headers. The batch route additionally rejects an unsupported content type, a zero generation, a body length outside the allowed set, a short body, or trailing bytes. It reads the full bounded body before submitting the first frame, so a connection that ends during upload cannot produce a partial batch.

A successful response is:

```json
{
  "status": "success",
  "runtime_id": "0123456789abcdef",
  "generation": 42,
  "accepted_frames": 5,
  "next_sequence": 105
}
```

Success means all frames were accepted by the local Unix datagram ingress. It does not claim that UDP/8302 reached or played on the physical device.

A stale runtime, generation, producer token or sequence mismatch returns `409 Conflict` with the authoritative runtime, generation and expected sequence where applicable. An active lease held by another producer returns `409 Conflict` with `error: producer_busy`; an expired token returns `error: session_expired`. An unavailable PCM socket, a full local queue, or a send failure returns `503 Service Unavailable` with the number of frames accepted and the next sequence. Invalid request syntax or shape returns `400 Bad Request`. A call that is not in `talking` state returns `409 Conflict`.

## Runtime, producer and retry rules

Doorfast exposes a random `runtime_id` in status and regenerates it at daemon start. The bridge stores the runtime ID, current generation, next expected frame sequence, active producer token and monotonic lease deadline in the locked `/tmp/doorfast-pcm-http.state` file. It opens the file without following symbolic links and accepts only a regular file owned by its effective user with mode `0600`. The Doorfast init script removes this volatile state during daemon start and restart.

Only the session-opening operation may initialize state for a new authoritative runtime or generation. Before changing state it takes the exclusive lock and reads Doorfast status again; the requested runtime and generation must still match a `talking` call with active audio transmission. Batch submission never resets runtime or generation. It accepts only the unexpired producer token stored in state. These rules prevent a suspended request from an old runtime or generation from reverting newer state.

The helper obtains the same nonblocking exclusive lock before comparing or advancing batch sequence state. The producer token remains held across requests, while the lock only serializes the short state transition and bounded batch submission. This prevents two browser tabs or HA clients from alternating adjacent sequences. It advances the expected sequence after every successful Unix datagram submission, including a successfully submitted prefix followed by a failure.

If an HTTP response is lost after frames were accepted, retrying the same sequence with a still-valid token returns a sequence mismatch with the advanced `next_sequence`; the client skips the accepted prefix and continues there. A client never retries frames below the returned sequence. A gap above the expected sequence is rejected rather than filled with synthesized silence.

When a request fails or times out, the browser or HA producer pauses capture and discards newly captured audio instead of building a delayed queue. It reads fresh Doorfast status before recovery. If its lease remains valid, it may use the sequence response to continue; after lease expiry it opens a new session and resumes at the server-provided sequence. A runtime or generation change ends the capture session and discards every buffered frame.

The producer token prevents accidental concurrent injection but is not a replacement for HTTP authentication, especially on an unencrypted network. The endpoint shares the existing Doorfast CGI access boundary. Deployments must restrict the CGI to trusted HA/router networks, and browser use requires HTTPS, until the project adds a common authentication mechanism for all control endpoints.

## Pacing and buffering

After validating authoritative status under the state lock, the helper schedules the first accepted frame immediately and subsequent frames against absolute monotonic 20 ms deadlines. It does not retry a successful Unix datagram send. It holds at most five frames, or 1600 bytes, in memory. A call ending during those 100 ms may cause the daemon's final generation check to discard later frames; HTTP success continues to mean local ingress acceptance rather than physical playback.

The Doorfast daemon continues to drain the Unix socket every 10 ms and submit at most one frame when its own 20 ms transmitter deadline is due. It continues to reject malformed frames, a different generation, a non-talking session or an unavailable learned audio route. This second check closes races where the call ends after the HTTP bridge reads status.

The HTTP bridge does not queue across requests. The client keeps at most one request in flight. If the client cannot maintain this rule, it must stop capture rather than accumulate delayed speech.

## Components

A new small C helper will own binary request reading, runtime and numeric validation, secure token generation, authoritative ubus status checks, state locking, monotonic leases and pacing, and calls to `df_gvs_pcm_ingress_send`. The existing shell CGI will select the three audio-session routes before reading a body into a shell variable and execute the helper. Binary PCM is never stored in a shell variable or log.

The main daemon will create and expose `runtime_id` but will not otherwise move HTTP logic into the call runtime. Existing clients may ignore the additional status field.

The helper will use fixed-size buffers and the existing PCM ingress serializer. Its normal output is a CGI response with JSON metadata; stderr contains bounded diagnostic categories and never PCM bytes. The package installs the helper beside `doorfast-pcm-submit` and increments the package release.

The later browser client will capture mono audio, resample to 8 kHz, form 160-sample frames, send no more than five frames per request, keep one request in flight, and stop on call-generation or connectivity changes. Browser microphone capture requires HTTPS or another browser-recognized secure context and will not be presented as available on an ordinary HTTP LuCI page.

## Verification

Unit tests will cover query and length validation, exact PCM preservation, runtime and generation changes, session creation and expiry, producer exclusion, sequence advance, duplicate and gap rejection, lock contention, partial send failure, absolute 20 ms pacing, stale requests and short-body rejection. Tests will suspend an old request across a simulated runtime/generation change to prove it cannot revert state.

HTTP bridge tests will use an injected status provider and isolated Unix datagram socket to verify response codes and ensure binary data bypasses shell variables. The production helper uses ubus and the fixed production socket; dependency-injected providers and socket overrides exist only in a separate acceptance build and cannot be selected through the installed CGI. Package tests will verify installation and dependencies. The x86_64 Actions job will upload this non-installed acceptance binary beside the APK so the VM runner can copy it to `/tmp`, execute it, and remove it.

The ImmortalWrt VM acceptance will install the resulting APK and split evidence into two executable layers. Against the real running daemon and installed CGI it verifies package wiring plus rejection of idle, stale, malformed and oversized requests. A separately built acceptance helper then uses synthetic talking status and an isolated Unix socket to verify:

- session creation, lease renewal, expiry and producer exclusion;
- a valid five-frame batch arrives in order with runtime and generation intact;
- frame delivery intervals are not shorter than the configured 20 ms cadence, within VM scheduling tolerance;
- a failed prefix reports the correct `accepted_frames` and `next_sequence`;
- a suspended old-runtime request cannot reset new state;
- restarting the HTTP producer does not replay accepted sequences.

The acceptance run also confirms the existing Doorfast service, event relay, network UCI and firewall state remain unchanged.

Physical MT8157 replacement testing remains required to establish actual loudspeaker format, intelligibility, latency, echo behavior and long-call resource use.

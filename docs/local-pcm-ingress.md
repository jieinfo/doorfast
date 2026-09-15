# Local PCM ingress

Doorfast accepts outbound talk audio from a local Unix datagram socket. This is
a Doorfast-local interface for a future Home Assistant or browser media bridge;
it is not part of the vendor GVS protocol.

The daemon creates `/var/run/doorfast-audio.sock` only when an active transport
is enabled. Passive observation mode does not create the socket.

Each datagram is exactly 336 bytes:

| Offset | Size | Value |
| --- | ---: | --- |
| `0x00` | 8 | `DFPCM01\0` |
| `0x08` | 8 | Current Doorfast call generation, little-endian and nonzero |
| `0x10` | 320 | 160 signed 16-bit little-endian PCM samples |

The samples represent 20 ms of mono 8 kHz audio. Every datagram carries the
call generation so queued data from an ended call cannot be accepted by a new
call. The receiver is nonblocking, preserves datagram boundaries, and creates
its socket with mode `0600`. It refuses to replace a regular file at the socket
path.

The runtime services the socket every 10 ms. It submits at most one current-call
frame whenever the 20 ms transmitter deadline is due. Before a call is active,
it drains and discards queued frames; during a call, it skips malformed frames
and frames carrying another generation. Each service pass examines at most 32
datagrams, which bounds runtime work even if a producer floods the socket.

Accepted samples pass through the session-bound audio transmitter, which
applies the talking-state, generation, pacing, A-law encoding, and
observed-route checks before UDP/8302 transmission. Doorfast does not synthesize
silence when no local frame is available.

The APK also installs `doorfast-pcm-submit` as the supported producer for this
private socket. It reads exactly one 320-byte, signed 16-bit little-endian PCM
frame from standard input and requires the current nonzero call generation:

```sh
producer | doorfast-pcm-submit <generation>
```

An optional second argument overrides the socket path for isolated tests. The
sender accepts only a real Unix datagram socket owned by its effective user with
mode `0600`; it rejects symbolic links and broader permissions. One invocation
produces one atomic datagram. Network bridges must pace calls at 20 ms and must
not retry a frame after a successful exit. The installed HTTP bridge described
below performs that pacing; any later WebRTC producer must preserve the same
rule.

Package release `0.1.0-r39` installs the network-facing
`doorfast-pcm-http` helper behind the existing CGI. Its session, batch and
release contract is documented in [Doorfast HTTP bridge](doorfast-http-bridge.md#submit-microphone-pcm).
It converts validated HTTP batches into the same `DFPCM01` datagrams and keeps
the Unix socket private. The producer token, runtime ID, generation, continuous
sequence, two-second lease and state-file lock prevent accidental mixing or
replay by concurrent clients; they do not replace access control for the CGI.

There are two separate acceptance boundaries. A successful HTTP response means
the helper submitted the indicated frames to this local Unix ingress. The main
daemon then performs another talking-state and generation check before it
encodes accepted samples as G.711 A-law and sends UDP/8302. Neither local Unix
acceptance nor a successful UDP send proves that an MT8157 speaker decoded or
played intelligible audio. Speaker format, audible quality, latency, echo and
long-call stability remain physical-device acceptance items.

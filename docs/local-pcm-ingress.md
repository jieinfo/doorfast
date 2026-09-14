# Local PCM ingress

Doorfast accepts outbound talk audio from a local Unix datagram socket. This is
a Doorfast-local interface for a future Home Assistant or browser media bridge;
it is not part of the vendor GVS protocol.

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

The ingress parser does not send network traffic by itself. Runtime integration
must submit a valid frame to the session-bound audio transmitter, which applies
the talking-state, generation, pacing, A-law encoding, and observed-route
checks before UDP/8302 transmission.

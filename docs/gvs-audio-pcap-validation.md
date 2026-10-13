# GVS audio PCAP validation

> **Evidence appendix:** This file preserves the captured-audio validation record. It does not define current features, configuration, or project status; the repository root `README.md` is authoritative.

> **Validation boundary (2026-10-12):** The captures and vendor analysis below
> are existing codec/protocol evidence. The receive regression added below uses
> synthetic data only. Physical speaker listening, HA continuous duplex use,
> echo/latency measurement and HTTPS acceptance remain unverified. HTTP voice
> is for trusted LANs; public voice access should use HTTPS through a proxy,
> without publicly exposing Doorfast CGI.

This note cross-checks the vendor APK analysis against five authorized captures
under `mt8157`. The captures contain 14,959 UDP packets on port 8302.

## Packet structure

Every observed 8302 packet begins with the ten-byte `GVSGVS A5A5A5A5` magic.
For the four captures without snap-length truncation, every declared payload
length equals the captured datagram length minus the 42-byte audio header. The
observed payload sizes are 128, 160, and 192 bytes. One disconnect capture used
a 256-byte capture limit, so some 192-byte payload packets are truncated in the
PCAP; those packets are excluded from byte-distribution checks.

The observed fixed fields match the vendor implementation:

| Field | Observed value |
| --- | --- |
| byte at `0x17` | `0` |
| payload length at `0x22` | `128`, `160`, or `192` |
| field at `0x1e` | `1` |
| field at `0x20` | `1` |
| field at `0x24` | `0x0100` |

## Codec evidence

Across complete payloads, `0xD5` and `0x55` are the dominant values in every
capture. These are the positive and negative near-zero codes produced by the
standard G.711 A-law mapping. The G.711 mu-law near-zero values `0xFF` and
`0x7F` occur only rarely. This agrees with the APK's GVS audio codec code, which
uses an A-law lookup/segment mapping and the `0x55` XOR transform.

The combined evidence supports decoding this GVS path as 8 kHz mono G.711
A-law. Payload duration is variable: 128, 160, and 192 samples correspond to
16, 20, and 24 milliseconds at 8 kHz, so the runtime must not require every
packet to contain exactly 160 bytes.

Direction separates those sizes. Across the five captures, every one of the
6,245 packets sourced by the original MT8157 address (`10.5.83.0`) carries a
160-byte payload. Packets sourced by the three door-station addresses use the
128- and 192-byte sizes. Doorfast therefore accepts all three sizes on receive,
while the MT8157 replacement transmit path emits 160 A-law samples every 20 ms.

The 16-bit little-endian sequence at offset `0x18` advances by one for nearly
every consecutive packet from a given IPv4 source. Its captured initial value
is not fixed, and the MT8157 sequence continues across destination changes and
capture boundaries. The transmit controller consequently preserves one
wrapping process-level sequence across call generations instead of resetting
it when a new call starts.

## Doorfast transmission regression

The project test suite now exercises the complete local transmit boundary. It
sends a serialized 336-byte `DFPCM01` Unix datagram, checks the active call
generation and pacing in the PCM pump, encodes 160 zero PCM samples to the
expected A-law value `0xd5`, resolves a route learned from a control packet, and
receives the resulting 202-byte GVS frame from a real UDP/8302 loopback socket.
The parsed frame must retain the current source and peer identities, sequence,
160-byte length fields, `d/e = 1`, and `f = 0x0100`.

This is Doorfast integration evidence (`D-T`). It checks the same shape seen in
the vendor static path and captures, but does not replace playback confirmation
from the physical door station (`D-F`).

## Receive integrity and sequencing

The media parsers require the UDP payload size to equal the media header plus
the length declared at offset `0x22`. Audio additionally requires field `c` at
offset `0x1a` to carry that same payload size, matching the vendor send path and
all complete authorized capture samples. A read-only pass over the five
authorized captures found 14,959 audio packets: 13,804 were captured completely,
1,155 were truncated by the capture limit, and none of the complete packets
violated this strict length rule. Truncated packets and packets with trailing
bytes are rejected before buffering or video reassembly.

The audio buffer treats the 16-bit sequence as a wrapping counter. It accepts a
forward jump while counting both the gap event and the number of missing
packets. An identical sequence is counted and discarded as a duplicate; a
sequence behind the current half-range window is counted and discarded as
late. These rejected packets do not change the buffer, accepted-packet totals,
or latest media timestamp. A new call generation resets the complete sequence
window and its counters.

The ubus `audio` status exposes `sequence_gaps`, `missing_packets`,
`duplicate_packets`, and `late_packets` separately for diagnostics. The earlier
direct-buffer policy above describes sequence accounting; v1 now puts a bounded
jitter stage before it, with four waiting slots, a 40 ms gap deadline and at
most 320 synthetic A-law samples for one gap. Full-window pressure may advance
before that deadline. Live audio quality and reorder timing still require
target-device measurement.

## Synthetic continuous receive regression

This is local software evidence (`D-T`), not a new capture or physical-device
result (`D-F`). `tests/test_audio_receive_pipeline.c` round-trips synthetic
128-, 160- and 192-byte payloads through the real GVS serializer and parser,
then connects the real jitter, buffer, playout and A-law decoder. Its publisher
does not write files: it checks every A-law byte and decoded PCM sample against
independent fixture values (`0xd5` → 8, `0x55` → -8, `0x00` → -5504,
`0x80` → 5504), and retains only counters, metadata and a PCM checksum.

| Source | Observed software result | Remaining acceptance |
| --- | --- | --- |
| Existing vendor codec and five authorized PCAPs above | A-law, 8 kHz mono, 42-byte header and variable payload sizes agree | Confirm current target source/destination and codec during an authorized call |
| Mixed-size receive fixture, sequence 65534 → 65535 → 0, duplicate, reorder and missing sequence 1 | Exact ordered 800 PCM samples: 608 real plus 192 concealed; four real packets, one duplicate and one late packet; callback at synthetic tick 200 ms | Listen for loss/reorder artifacts on target hardware |
| Generation 7 → 8 and close-to-zero fixture | Both pending samples and queued gap audio are cleared; stale packets/emissions rejected; new generation outputs only its 480 expected samples | Exercise HA restart, hangup, preemption and network interruption |
| Twenty serialized 160-sample packets and failing publisher | Export newest 1600 of 3200 samples, mark dropped/discontinuity, preserve cursor on failure and retry at tick 300 after failure at 200 | Measure sustained playback, echo and actual end-to-end delay |
| Chunk-store/runtime/CGI tests in the full regression | Cover 20 retained blocks, atomic publication, lifecycle reset, uint64 limits, identity/304 and restart races; selected WAV is checked byte for byte | Verify real ImmortalWrt/ubus integration and HTTPS proxy behavior |

The fixture checks no callback at tick 159 before the 40 ms gap deadline and
no second-generation callback at 509 before its short-block deadline at 510.
A temporary mutation that skipped playout reset for an already bound generation
made the test fail on an extra old-generation publisher callback, incorrect PCM and
callback time; the mutation was immediately restored and the focused runner
passed. It is not part of the code. This establishes that reset assertions
detect old audio leakage rather than only checking a mock invocation.

In v1, `Timestamp-Ms` is the selected chunk's monotonic publication time and
`Latest-Timestamp-Ms` comes from the latest publication status after copying.
Their difference measures publication backlog, not precise capture time or
end-to-end delay. Neither synthetic tick values nor those HTTP headers prove
physical latency below a target.

From the repository root, rebuild and explicitly run the C suite before the
HTTP/package/UI compatibility checks:

```sh
set -e
make test
build/doorfast-tests
make doorfast
sh tests/test_doorfast_http.sh
sh tests/test_package_manifest.sh
node tests/test_luci_status.js
node tests/test_luci_media.js
node tests/js/test_media_status.mjs
git diff --check
```

For a focused rerun after building: `build/doorfast-tests --audio-receive-pipeline`.
Current v1 request parameters, headers and client recovery rules are documented
in the [README HTTP API](../README.md#读取连续下行音频-v1). The follow-up HA and
field acceptance must record counts, digests and metadata only, without raw
voice recordings; it must keep software, HTTPS and listening outcomes separate.

## Reproduction

List the audio packets in a capture:

```text
tcpdump -nn -r capture.pcap 'udp port 8302'
```

The protocol parser must continue to use the captured UDP length and reject
truncated packets before decoding.

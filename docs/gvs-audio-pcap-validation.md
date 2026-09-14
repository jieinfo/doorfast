# GVS audio PCAP validation

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

## Reproduction

List the audio packets in a capture:

```text
tcpdump -nn -r capture.pcap 'udp port 8302'
```

The protocol parser must continue to use the captured UDP length and reject
truncated packets before decoding.

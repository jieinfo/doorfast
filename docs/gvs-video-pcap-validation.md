# GVS video PCAP validation

Doorfast cross-checks the vendor-delivered `Snippet/012-video-business.md`
layout against the five authorized captures under the local `mt8157` data set.
The captures contain 56,759 UDP/8303 packets. Of those, 26,773 contain a
complete captured datagram and 29,986 are truncated by the original capture
limit.

Every complete packet has a 1200-byte chunk-capacity field. All complete
packets also satisfy these relationships:

- UDP payload length equals the 38-byte video header plus `chunkLength`;
- `chunkCount` equals the ceiling of `fullLength / capacity`;
- the chunk offset is `(chunkIndex - 1) * capacity`;
- every non-final chunk has capacity bytes;
- the final chunk contains exactly the remaining frame bytes.

The runtime parser rejects incomplete datagrams before reassembly. The
reassembler accepts a capacity from 1 through 1200 but requires all geometry to
remain identical for one frame and caps complete frames at 1 MiB.

Unlike the vendor APK's strict sequential receiver, Doorfast stores valid
chunks by their declared index. This permits bounded network reordering without
changing the GVS wire format. Identical duplicates are ignored, conflicting
duplicates are rejected without corrupting accepted data, and a delayed older
frame cannot replace the current frame. A newer frame abandons an incomplete
older frame, and the 16-bit frame counter may wrap from 65535 to zero.

Unit tests cover out-of-order completion, identical and conflicting duplicates,
late frames, forward frame replacement, invalid geometry, preserved state after
an invalid chunk, and frame-number wraparound. This is project robustness
evidence (`D-T`); physical latency, loss patterns, and decoder behavior still
require device testing (`D-F`).

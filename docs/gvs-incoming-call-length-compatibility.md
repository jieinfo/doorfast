# GVS incoming-call length compatibility

Doorfast r23 accepts one receive-only field format observed in four unanswered
calls to logical identity 1901 across three GVS door-station identities.

The affected `03/01` datagram has a complete 51-byte GVS frame: a 42-byte common
header followed by nine captured payload bytes, while the little-endian field at
offsets 40–41 contains 15. Ethernet and UDP captured lengths equal their wire and
declared lengths, excluding PCAP truncation.

The parser accepts the discrepancy only when all of these conditions hold:

- family/opcode is exactly `03/01`;
- the complete datagram has at least the 42-byte common header;
- the declared length is exactly six greater than the captured payload length.

The exposed payload length is the nine bytes actually available. Every other
family/opcode retains exact `42 + declared == actual` validation. Nearby biases,
one-byte truncation, one-byte extension, and applying the exception to `03/04` or
`04/01` are regression-tested and rejected.

This changes parsing and evidence classification only. It does not transmit a
packet, enable host mode, alter the bridge, or infer that all GVS length fields use
the same convention.

# GVS media admission

Doorfast accepts UDP/8302 audio and UDP/8303 video only while a current call
generation is in preview, ringing, or talking state. After parsing the media
header, the runtime requires both six-byte logical addresses to match:

- the media destination must equal the configured Doorfast identity;
- the media source must equal the current session peer.

This also rejects outbound Doorfast audio observed by the capture interface,
because its source and destination are reversed relative to received media. It
therefore cannot be appended to the received-audio buffer as local echo.

The rule is supported by the vendor-delivered compatibility material, which
requires a complete six-byte source match during an existing call. It is also
cross-checked against the authorized
`pcap/gvs-incoming-three-20260907.pcap` capture (SHA-256
`dfc712339604ea90427c4a900f40570acfbcf0ddbeafb5a40ca0cb9db3e7e367`):
the sampled inbound UDP/8302 and UDP/8303 headers contain the complete indoor
station identity as destination and the active door station as source.

Unit tests cover preview, ringing, talking, ended and idle states, a zero
generation, another indoor extension, another peer extension, reversed
outbound endpoints, and invalid arguments. This provides static/capture and
project-test evidence; multi-device field behavior remains an entity-device
acceptance item.

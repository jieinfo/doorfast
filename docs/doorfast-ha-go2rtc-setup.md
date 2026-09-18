# Doorfast HA go2rtc RTSP ingress

Create an empty stream before Doorfast starts publishing. go2rtc's RTSP
publisher accepts an external `ANNOUNCE`/`RECORD` producer only for a
pre-created stream:

```yaml
streams:
  doorfast_preview:

rtsp:
  listen: ":8554"
  username: doorfast
  password: ${DOORFAST_RTSP_PASSWORD}
  default_query: "video=h264"
```

Keep the go2rtc API (`1984/TCP`) authenticated and reachable only from Home
Assistant administration. Viewers use the Home Assistant WebRTC path on
`8555/TCP+UDP`; they do not connect to Doorfast or to the building-facing GVS
interface. Doorfast needs only the HA host's `8554/TCP` ingress and the
configured `doorfast_preview` stream name.

Store the RTSP password in `/etc/doorfast/media-credentials` with mode `0600`.
The Doorfast status endpoint and acceptance artifacts report only whether a
credential is set. Do not put a password, complete RTSP URL, SDP, ICE data, or
raw GVS/JPEG/RTP payload in logs, diagnostics, support bundles, or screenshots.

Run the repeatable local contract check from the repository root:

```sh
python3 -B tests/run_doorfast_vm_media.py
```

The check proves RTSP publication and cleanup using synthetic fixtures. It does
not prove that a real door station accepts the monitor request or that live,
physical video reaches Home Assistant. Physical door-station and field-video
acceptance remain separate release gates.

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

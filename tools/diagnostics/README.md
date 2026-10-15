# Time-query A/B probe

Temporary experiment tool, **not a Doorfast feature or an installed package**.
It does not start calls, acquire audio, unlock doors, change configuration,
bind UDP/8300, or restart services. The operator controls one call through HA
and separately captures control packets and media **headers only**.

The default is observe-only. `--send-vendor-time-query` enables at most two
vendor-short `03/56` queries after one observed current call/ACK. This deliberately
reproduces the reference device's declared-one-byte/actual-zero-byte quirk;
production serialization and parsing remain unchanged. It is not a general
packet replay/injection interface.

## Build and verify

```sh
make time-query-probe-test
make time-query-probe
sh scripts/build-time-query-probe-sdk.sh /path/to/prepared-sdk artifacts/doorfast-time-query-probe
```

The host build cannot read live ubus state and fails closed before capture.
Only the SDK build enables that adapter. CI publishes a separate
`doorfast-time-query-probe` artifact with a SHA256 file, never an APK payload.
Match the artifact's workflow commit to the reviewed PR commit before use.
After extraction, verify `sha256sum -c time-query-probe.sha256`, transfer the
verified binary only to a fresh `/tmp` path, and set its mode to 0700.

## Run a bounded experiment

Use `--help` for the required local interface, IPv4/logical addresses, station,
runtime ID and **next** operation ID. Do not store credentials in arguments.
Do not run against a preexisting preview/call, or while another controller is
testing. Start the probe before the one authorized HA call; wait for its `ready`
event. Abort without calling if it does not become ready.

Preflight requires the selected runtime, previous operation, idle native state
and no media sessions. After observing a fresh unfragmented Ethernet/IPv4
`03/03` with the fixed 8303/8302 ports and 120-second request, it binds the
current operation/control generation. Only a matching `03/83` can open the
100ms window; each send requires a fresh native TALKING snapshot with matching
runtime/station/operation/control keys. The final IO boundary rechecks time.
No old PCAP or saved opaque header fields are reused: the outgoing Ethernet
route is learned from this call, and new header random/transform bytes are made.

Ownership is polled every 50ms (each status request has a 20ms timeout) and
rechecked on candidate control packets and before sends. Any observed conflict
or read failure permanently cancels this process. Inbound `03/01` addressed to
this indoor identity also cancels it even from a different peer; only that
control header is inspected, with no caller payload parsed, copied or logged.
An ended unrelated call cannot restore a cancelled probe.

The probe lifetime is at most 35 seconds of observation, with bounded ubus
status requests; SIGINT/SIGTERM cancel it. Initial ubus connection/object lookup
occurs before `ready`, and may fail or take longer than the short status timeout.
Driver readiness timeout must stop the probe and must not start a call.
It cannot atomically lock the daemon's owner across an external Ethernet write;
the status-to-send race is an explicit experimental limitation, not a production
control API. It must never be used for unlock or audio transmission.

## Interpret results

`sent=2` and `valid_treatment=true` mean only that both writes succeeded within
the local time guard, **not** peer acceptance or a repaired call. Check the
independent wire capture for both queries, `03/57` values and their timestamps.
One/no/late query is an invalid B sample; do not silently replace it or expand
the six-call budget. Observe-only A sends no query. Preserve early peer hangup
as a result rather than calling it a transport failure.

Record the first audio/video headers and terminal direction, plus the later
`03/57=5` values and current countdown. Keep microphone, stop byte, retry cadence,
cooldown, call duration and all configurations unchanged. Stop on runtime/owner
conflicts or unrelated incoming calls; only clean up the exact call created by
the driver. This probe is not evidence of full-duplex/browser audio acceptance.

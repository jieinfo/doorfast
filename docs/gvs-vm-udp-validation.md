# Isolated x86_64 UDP validation

Validated on 2026-09-09 with ImmortalWrt 25.12.1 x86/64 and `doorfast-0.1.0-r7.apk` from successful GitHub run 34311582809 (commit `523b825`). The core APK SHA-256 is `701e4e8d6bbda83d80bd236af0d1dae0ee77449f5e2eb2b68aa225a120139a5e`; the installed daemon is an x86-64 musl ELF with SHA-256 `26e61740f5e80f3972fe90403f84cd634290cd2e0fe948dc104ad7b630874b3c`.

The test-only `make peer-udp-inject` tool sends one built-in synthetic frame to fixed loopback destination `127.0.0.1:18300`. QEMU forwards that UDP port to guest port 8300. No destination or arbitrary payload arguments are supported. Header authentication fields are synthetic fixtures; this test does not establish real-device authentication compatibility. The fixed scenarios include a 44-byte `07/01` candidate probe for validating the passive pending-reply and simulated transaction path in the validated `r7` build.

VM prerequisites: passive Doorfast on `br-lan`, synthetic identity `IS:2-1-101-2`, and QEMU user networking with `hostfwd=udp:127.0.0.1:18300-:8300`.

Run from the repository root, with the VM running:

```sh
python3 -B tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh
```

Add `--wait-for-takeover` to stop periodic input and wait for the two real 60-second deadlines.

The runner restarts Doorfast, waits for `sync_ask`, receives a fixed `91/81` election reply, then injects two fixed 44-byte `07/01` candidate probes. An `r7` daemon must log `accepted=1 reply_pending=1 peer_observed=1 mode=passive` and a new simulated transaction success for each probe. The runner next injects a fixed 48-byte `07/81` candidate-online reply and requires `online_peers=1`. It then injects fixed `Period` version 7 and `Normal` version 8 JSON frames. The former refreshes follower maintenance while preserving the old implementation's version-0 behavior; the latter changes the runtime version to 8 and must persist it to `/etc/config/doorfast-sync`. Finally, it injects a 42-byte `03/01` local call and checks for a new `IncomingCall` log event. In takeover mode, it also requires the candidate to return offline after its real 60-second deadline, verifies the first missed period remains follower with count 1, the second changes the role to maintainer, and takeover emits a passive `periodic_sync` action.

The `r5` result verifies QEMU forwarding, Ethernet capture, UDP extraction, complete common-header parsing, `07/01` pending-reply handling, candidate online refresh, runtime election integration, synchronization version persistence, and call-event dispatch in the built x86_64 APK. The observed probe log was `peer_probe accepted=1 reply_pending=1 peer_observed=1 mode=passive`; the independent `07/81` log was `peer_reply accepted=1 mode=passive`. Capture buffering requires bounded polling rather than a fixed short sleep. A reply received after election can be parsed successfully without changing the role.

The `r7` target run observed two `sending`/`success` pairs with `attempt=1 timed_out=0 mode=simulated`. This proves that the built x86_64 APK drains each pending FIFO item through the transaction state machine; it does not prove that a `07/81` frame was serialized, transmitted, or accepted by a door station.

It does not verify real peer recognition, outbound periodic maintenance, real common-header compatibility, media, or physical control. Those remain separate milestones. Neither peer simulator nor UDP injector is packaged in the APK; CI checks generated package lists for the `gvs-peer-` prefix.

Local UDP unit tests bind port 18300 themselves, so stop the VM before running `python3 -B -m unittest tests/test_gvs_peer_udp.py`.

The r7 run used the short validation mode. The 60-second offline deadline and two-period takeover were previously verified with r4 and were not rerun for r7. The simulated transaction logs confirm receive-to-queue-to-transaction flow; absence of transmission follows from the current source implementation, not from a dedicated outbound packet capture.

# Isolated x86_64 UDP validation

Validated on 2026-09-09 with ImmortalWrt 25.12.1 x86/64 and `doorfast-0.1.0-r4.apk` from successful GitHub run 34295396530 (commit 731d6e0). The core APK SHA-256 is `518ed2bb05121250112b84b5214af47ab663db64feb7442308c65da2bb2517d4`.

The test-only `make peer-udp-inject` tool sends one built-in synthetic frame to fixed loopback destination `127.0.0.1:18300`. QEMU forwards that UDP port to guest port 8300. No destination or arbitrary payload arguments are supported. Header authentication fields are synthetic fixtures; this test does not establish real-device authentication compatibility. The fixed scenarios now include a 44-byte `07/01` candidate probe for validating the passive pending-reply path after `r5` is built.

VM prerequisites: passive Doorfast on `br-lan`, synthetic identity `IS:2-1-101-2`, and QEMU user networking with `hostfwd=udp:127.0.0.1:18300-:8300`.

Run from the repository root, with the VM running:

```sh
python3 -B tests/run_gvs_vm_udp.py /absolute/path/to/vm/ssh.sh
```

Add `--wait-for-takeover` to stop periodic input and wait for the two real 60-second deadlines.

The runner restarts Doorfast, waits for `sync_ask`, then injects a fixed 44-byte `07/01` candidate probe. An `r5` daemon must log `accepted=1 reply_pending=1 peer_observed=1 mode=passive`; it must not send the pending reply. The runner next injects a fixed 48-byte `07/81` candidate-online reply and requires `online_peers=1`. It also injects a 44-byte `91/81` reply and waits for actual reception, followed by fixed `Period` version 7 and `Normal` version 8 JSON frames. The former refreshes follower maintenance while preserving the old implementation's version-0 behavior; the latter changes the runtime version to 8 and must persist it to `/etc/config/doorfast-sync`. Finally, it injects a 42-byte `03/01` local call and checks for a new `IncomingCall` log event. In takeover mode, it also requires the candidate to return offline after its real 60-second deadline, verifies the first missed period remains follower with count 1, the second changes the role to maintainer, and takeover emits a passive `periodic_sync` action.

The published `r4` result verifies QEMU forwarding, Ethernet capture, UDP extraction, complete common-header parsing, candidate online refresh and timeout, runtime election integration, and call-event dispatch in the built x86_64 APK. Capture buffering requires bounded polling rather than a fixed short sleep. A reply received after election can be parsed successfully without changing the role. The new `07/01` assertion is prepared but remains pending until the `r5` artifact is installed in the VM.

It does not verify periodic JSON maintenance over the VM path, real peer recognition, media, or physical control. Those remain separate milestones. Neither peer simulator nor UDP injector is packaged in the APK; CI checks generated package lists for the `gvs-peer-` prefix.

Local UDP unit tests bind port 18300 themselves, so stop the VM before running `python3 -B -m unittest tests/test_gvs_peer_udp.py`.

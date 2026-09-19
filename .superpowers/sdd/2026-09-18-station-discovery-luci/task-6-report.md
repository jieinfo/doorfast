# Task 6 Report

Task 6 consolidates media status handling around the existing event stream and
Home Assistant event relay. The separate media relay implementation and its
test were removed, including the media relay queue, callbacks, credentials,
configuration, package sources, and status fields. The existing
`event_relay` implementation remains packaged and covered by its init test.

The public media ABI remains version 2. The former relay URL, relay callback,
and relay failure fields remain as deprecated, ignored slots with their
original order, offsets, sizes, and types. Module initialization clears those
slots and no media code invokes them. The stale `trace.last_json`,
`df_media_relay_tick`, and `module.relay` test references were removed.

Media credentials now contain only `rtsp_password`. The package upgrade
migration writes a temporary file, preserves the source owner and group,
sets mode `0600`, atomically moves the file into place, and removes temporary
files on every failure path. The migration emits neither credential value.

Verification completed successfully:

* `make clean && make test`
* `sh tests/test_media_credential_migration.sh`
* `sh tests/test_package_manifest.sh`
* `sh tests/test_doorfast_init.sh`
* `sh tests/test_event_relay_init.sh`
* `node tests/test_luci_media.js`
* `node tests/js/test_media_status.mjs`
* `git diff --check`

## Fix Round 1/5 Evidence

The package releases now advance to `doorfast` 53, `doorfast-media` 3, and
`luci-app-doorfast` 17. The main package owns the retained event relay's
`+ca-bundle` dependency while the media package continues to omit `libcurl`.
The manifest test asserts the release values and that the main release is
newer than the base release 52. Host CI runs the credential migration test.

The migration test now detects GNU and BSD `stat` modes, captures both output
streams on success and failure, checks that credential values never appear,
and injects `awk`, `chmod`, `chown`, and `mv` failures while verifying the
original file remains intact and temporary files are removed. It passed on
the host and under `alpine:3.20` BusyBox userland.

`status_revision` now tracks the exported media snapshot fields: monitor
state, generation, failure, encoder running state, and queue drops. A cached
snapshot increments the revision exactly once for a changed public field and
does not increment for hidden-only `media_ready` changes or repeated no-op
updates. Unit coverage asserts viewer, encoder, queue, stop, timeout/failure,
and no-op transitions individually.

Fix-round verification:

* `make clean && make test`
* `sh tests/test_media_credential_migration.sh`
* `docker run --rm -v <worktree>:/work -w /work alpine:3.20 sh tests/test_media_credential_migration.sh`
* `sh tests/test_package_manifest.sh`
* `sh tests/test_doorfast_init.sh`
* `sh tests/test_event_relay_init.sh`
* `node tests/test_luci_media.js`
* `node tests/js/test_media_status.mjs`
* `git diff --check`

Targeted scans found no `trace.last_json`, `df_media_relay_tick`, or
`module.relay` references in source or tests, and no media relay source is
included in either package build.

Supplemental commit `fix: finish media relay removal` includes the two LuCI
changes that were already present in the Task 6 worktree: media status no
longer requires or renders relay fields, and the media page no longer exposes
relay URL/token controls or sends relay credential parameters.

Supplemental verification:

* `node tests/test_luci_media.js`
* `node tests/js/test_media_status.mjs`
* `node --check package/luci-app-doorfast/htdocs/luci-static/resources/doorfast/status_model.js`
* `node --check package/luci-app-doorfast/htdocs/luci-static/resources/view/doorfast/media.js`
* `git diff --check`

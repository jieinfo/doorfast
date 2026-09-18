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

Targeted scans found no `trace.last_json`, `df_media_relay_tick`, or
`module.relay` references in source or tests, and no media relay source is
included in either package build.

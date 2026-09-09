#!/bin/sh
set -eu

make doorfast
./build/doorfast --help | grep -q '^Usage: doorfast '
./build/doorfast --config tests/fixtures/doorfast-disabled.conf | grep -q '^doorfast: disabled$'
if ./build/doorfast --config tests/fixtures/legacy-doorlink.conf >/dev/null 2>&1; then
    echo 'legacy configuration unexpectedly accepted as runtime configuration' >&2
    exit 1
fi
if capture_error=$(./build/doorfast --config tests/fixtures/doorfast-missing-interface.conf 2>&1); then
    echo 'missing capture interface unexpectedly started' >&2
    exit 1
fi
printf '%s\n' "$capture_error" | grep -q 'doorfast-no-such-interface'
output=$(./build/doorfast --import-legacy tests/fixtures/legacy-doorlink.conf)
printf '%s\n' "$output" | grep -q "option brand 'dnake'"
printf '%s\n' "$output" | grep -q "option enabled '0'"
printf '%s\n' "$output" | grep -q "option unlock '-1'"
! printf '%s\n' "$output" | grep -q 'license-is-never-exported'
! printf '%s\n' "$output" | grep -q 'token-is-never-exported'

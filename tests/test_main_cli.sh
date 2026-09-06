#!/bin/sh
set -eu

make doorfast
./build/doorfast --help | grep -q '^Usage: doorfast '
output=$(./build/doorfast --import-legacy tests/fixtures/legacy-doorlink.conf)
printf '%s\n' "$output" | grep -q "option brand 'dnake'"
printf '%s\n' "$output" | grep -q "option enabled '1'"
printf '%s\n' "$output" | grep -q "option unlock '-1'"
! printf '%s\n' "$output" | grep -q 'license-is-never-exported'
! printf '%s\n' "$output" | grep -q 'token-is-never-exported'

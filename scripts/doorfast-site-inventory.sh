#!/bin/sh
# Read-only system queries; the only writes are a new private inventory directory.
set -eu
umask 077
ulimit -f 2048
test "$#" -eq 1
test "$1" = /mnt/doorfast/inventory
test -d /mnt/doorfast
test ! -L /mnt/doorfast
mountpoint -q /mnt/doorfast
mkdir -p /mnt/doorfast/inventory
test ! -L /mnt/doorfast/inventory
test "$(stat -c %a /mnt/doorfast/inventory)" = 700
output=$(mktemp -d /mnt/doorfast/inventory/snapshot-XXXXXXXX)
failed=0
capture() {
    name=$1
    shift
    if ! "$@" >"$output/$name" 2>"$output/$name.err"; then
        failed=1
    fi
    # Bound stored diagnostic output, and treat truncation as failure.
    if [ "$(wc -c <"$output/$name")" -gt 1048576 ]; then
        failed=1
        : >"$output/$name"
    fi
}
capture links.json ip -s -d -j link show
capture addresses.json ip -j address show
capture routes.json ip -j route show table all
capture firewall.json nft -j list ruleset
capture hashes.txt sha256sum /etc/config/network /etc/config/firewall /etc/config/dhcp /etc/config/doorfast /etc/config/doorfast-deployment
capture disk.txt df -Pk /mnt/doorfast
capture mounts.txt mount
capture status.json ubus call doorfast status
capture passive.txt uci -q get doorfast.main.passive_only
capture version.txt apk info doorfast
capture preflight.json /usr/sbin/doorfast --preflight /etc/config/doorfast-deployment
printf '{"schema_version":1,"query_failed":%s}\n' "$failed" >"$output/manifest.json"
printf '%s\n' "$output"
exit "$failed"

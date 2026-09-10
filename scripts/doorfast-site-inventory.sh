#!/bin/sh
# Read-only system queries; the only writes are a new private inventory directory.
set -eu
umask 077
ulimit -f 2048
test "$#" -eq 1
test "$1" = /mnt/doorfast/inventory
test -d /mnt/doorfast
test ! -L /mnt/doorfast
grep -q ' /mnt/doorfast ' /proc/mounts
mkdir -p /mnt/doorfast/inventory
test ! -L /mnt/doorfast/inventory
chmod 700 /mnt/doorfast/inventory
test "$(ls -ld /mnt/doorfast/inventory | cut -c1-10)" = drwx------
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
capture version.txt apk info -e doorfast
if ! /usr/sbin/doorfast --preflight /etc/config/doorfast-deployment \
        >"$output/preflight.json" 2>"$output/preflight.json.err"; then
    # An unsafe deployment intentionally exits 2 but still yields useful JSON.
    test -s "$output/preflight.json" || failed=1
fi
printf '{"schema_version":1,"query_failed":%s}\n' "$failed" >"$output/manifest.json"
printf '%s\n' "$output"
exit "$failed"

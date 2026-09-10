#!/bin/sh
# Read-only system queries; the only writes are a new private inventory directory.
set -eu
umask 077
ulimit -f 2048
test "$#" -eq 1
root=${DOORFAST_INVENTORY_ROOT:-/mnt/doorfast}
inventory=$root/inventory
doorfast=${DOORFAST_INVENTORY_DOORFAST:-/usr/sbin/doorfast}
test "$1" = "$inventory"
test -d "$root"
test ! -L "$root"
mount | grep -Fq " on $root "
mkdir -p "$inventory"
test ! -L "$inventory"
chmod 700 "$inventory"
test "$(ls -ld "$inventory" | cut -c1-10)" = drwx------
output=$(mktemp -d "$inventory/snapshot-XXXXXXXX")
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
capture disk.txt df -Pk "$root"
capture mounts.txt mount
capture status.json ubus call doorfast status
capture passive.txt uci -q get doorfast.main.passive_only
capture version.txt apk list --installed doorfast
if ! "$doorfast" --preflight /etc/config/doorfast-deployment \
        >"$output/preflight.json" 2>"$output/preflight.json.err"; then
    # An unsafe deployment intentionally exits 2 but still yields useful JSON.
    test -s "$output/preflight.json" || failed=1
fi
printf '{"schema_version":1,"query_failed":%s}\n' "$failed" >"$output/manifest.json"
printf '%s\n' "$output"
exit "$failed"

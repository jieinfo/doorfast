#!/bin/sh
set -eu

workspace=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-site-inventory.XXXXXXXX")
cleanup() {
	status=$?
	trap - EXIT
	rm -rf "$workspace"
	exit "$status"
}
trap cleanup EXIT
root=$workspace/mnt-doorfast
fakebin=$workspace/bin
trace=$workspace/trace
mkdir -p "$root" "$fakebin"
: >"$trace"

for command in ip nft sha256sum df mount ubus uci apk doorfast; do
	ln -s "$PWD/tests/fixtures/site-inventory/command" "$fakebin/$command"
done

mode() {
	if stat -c %a "$1" >/dev/null 2>&1; then
		stat -c %a "$1"
	else
		stat -f %Lp "$1"
	fi
}

snapshot=$(PATH="$fakebin:$PATH" \
	DOORFAST_INVENTORY_ROOT="$root" \
	DOORFAST_INVENTORY_DOORFAST=doorfast \
	DOORFAST_INVENTORY_TEST_TRACE="$trace" \
	sh scripts/doorfast-site-inventory.sh "$root/inventory")

test -d "$snapshot"
test "$(mode "$root/inventory")" = 700
test "$(mode "$snapshot")" = 700
for file in manifest.json links.json addresses.json routes.json firewall.json \
		hashes.txt disk.txt mounts.txt status.json passive.txt version.txt preflight.json; do
	test -f "$snapshot/$file"
	test "$(mode "$snapshot/$file")" = 600
done
python3 -c 'import json,sys; assert json.load(open(sys.argv[1])) == {"schema_version": 1, "query_failed": 0}' \
	"$snapshot/manifest.json"
python3 - "$snapshot" <<'PY'
import json
import pathlib
import sys

snapshot = pathlib.Path(sys.argv[1])
links = {item["ifname"]: item for item in json.loads((snapshot / "links.json").read_text())}
assert links["door-up"]["address"] == "02:00:00:00:00:01"
assert "LOWER_UP" in links["door-up"]["flags"]
assert links["door-up"]["master"] == "br-door"
assert links["door-down"]["address"] == "02:00:00:00:00:02"
addresses = json.loads((snapshot / "addresses.json").read_text())
assert addresses == [{"ifname": "br-lan", "addr_info": [{"local": "192.0.2.1"}]}]
routes = json.loads((snapshot / "routes.json").read_text())
assert routes == [{"dst": "default", "dev": "br-lan", "gateway": "192.0.2.254"}]
status = json.loads((snapshot / "status.json").read_text())
assert status == {"running": True, "mode": "passive"}
preflight = json.loads((snapshot / "preflight.json").read_text())
assert preflight == {"safe": True, "failures": []}
PY
grep -Fxq '1' "$snapshot/passive.txt"
grep -Fq 'doorfast-0.1.0-r20 x86_64' "$snapshot/version.txt"
grep -Fq '/dev/test 32768000 1024 32766976 1% ' "$snapshot/disk.txt"
grep -Fxq 'ip -s -d -j link show' "$trace"
grep -Fxq 'ip -j address show' "$trace"
grep -Fxq 'ip -j route show table all' "$trace"
grep -Fxq 'apk list --installed doorfast' "$trace"
grep -Fxq 'doorfast --preflight /etc/config/doorfast-deployment' "$trace"

if awk '{ for (i = 1; i <= NF; i++) if ($i ~ /^(set|add|delete|commit|reload|restart|apply)$/) exit 1 }' "$trace"; then
	:
else
	echo 'inventory invoked a mutating command' >&2
	exit 1
fi

: >"$trace"
set +e
PATH="$fakebin:$PATH" \
	DOORFAST_INVENTORY_ROOT="$root" \
	DOORFAST_INVENTORY_DOORFAST=doorfast \
	DOORFAST_INVENTORY_TEST_TRACE="$trace" \
	DOORFAST_INVENTORY_TEST_FAIL=ubus \
	sh scripts/doorfast-site-inventory.sh "$root/inventory" >"$workspace/failed-path"
status=$?
set -e
test "$status" -eq 1
failed_snapshot=$(cat "$workspace/failed-path")
python3 -c 'import json,sys; assert json.load(open(sys.argv[1])) == {"schema_version": 1, "query_failed": 1}' \
	"$failed_snapshot/manifest.json"

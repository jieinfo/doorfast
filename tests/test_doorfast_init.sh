#!/bin/sh
set -eu

trace=$(mktemp)
trap 'rm -f "$trace"' EXIT

current_config=
doorfast_enabled=0
doorfast_call_elev=1
doorfast_active_host=0
doorfast_gvs_interface=door0
doorfast_passive_interface=door0
doorfast_host_interface=host0
doorfast_gvs_identity=IS:2-1-101-1
doorfast_indoor_ipaddr=
doorfast_indoor_netmask=255.0.0.0
automation_call_elev=
group_present=0
group_creation_succeeds=1

config_load() {
	current_config=$1
}

config_get() {
	value=
	if [ "$current_config" = doorfast-automation ] &&
	   [ "$3" = call_elev ]; then
		value=$automation_call_elev
	fi
	if [ "$current_config" = doorfast ]; then
		case "$3" in
			gvs_interface) value=$doorfast_gvs_interface ;;
			passive_interface) value=$doorfast_passive_interface ;;
			host_interface) value=$doorfast_host_interface ;;
			gvs_local_address) value=$doorfast_gvs_identity ;;
			indoor_ipaddr) value=$doorfast_indoor_ipaddr ;;
			indoor_netmask) value=$doorfast_indoor_netmask ;;
		esac
	fi
	eval "$1=\$value"
}

config_get_bool() {
	value=$4
	if [ "$current_config" = doorfast ]; then
		case "$3" in
			enabled) value=$doorfast_enabled ;;
			active_host) value=$doorfast_active_host ;;
			call_elev) value=$doorfast_call_elev ;;
		esac
	fi
	eval "$1=\$value"
}

uci() {
	printf '%s\n' "$*" >>"$trace"
}

group_exists() { [ "$group_present" -eq 1 ]; }
group_add_next() {
	printf 'group_add_next %s\n' "$*" >>"$trace"
	[ "$group_creation_succeeds" -eq 1 ] && group_present=1
	return 0
}
mkdir() { printf 'mkdir %s\n' "$*" >>"$trace"; }
chown() { printf 'chown %s\n' "$*" >>"$trace"; }
chmod() { printf 'chmod %s\n' "$*" >>"$trace"; }
rm() { printf 'rm %s\n' "$*" >>"$trace"; }
ip() { printf 'ip %s\n' "$*" >>"$trace"; }

procd_open_instance() { printf 'procd_open_instance\n' >>"$trace"; }
procd_set_param() { :; }
procd_close_instance() { :; }
procd_add_reload_trigger() { :; }

. package/doorfast/files/doorfast.init
HOST_ADDRESS_STATE="$trace.host-address"

start_service
grep -Fxq -- '-q set doorfast-automation.main.call_elev=1' "$trace"
grep -Fxq -- '-q commit doorfast-automation' "$trace"

: >"$trace"
automation_call_elev=0
start_service
test ! -s "$trace"

: >"$trace"
automation_call_elev=
doorfast_call_elev=0
start_service
grep -Fxq -- '-q set doorfast-automation.main.call_elev=0' "$trace"

: >"$trace"
doorfast_enabled=1
start_service
grep -Fq 'group_add_next doorfast' "$trace"
grep -Fq 'mkdir -p /var/run/doorfast' "$trace"
grep -Fq 'chown root:doorfast /var/run/doorfast' "$trace"
grep -Fq 'chmod 0750 /var/run/doorfast' "$trace"
cleanup_line=$(grep -nFx -- 'rm -f /tmp/doorfast-pcm-http.state' "$trace" | cut -d: -f1)
open_line=$(grep -nFx -- 'procd_open_instance' "$trace" | cut -d: -f1)
test "$cleanup_line" -lt "$open_line"

: >"$trace"
group_present=0
group_creation_succeeds=0
if start_service; then
	echo 'start_service accepted a missing doorfast group' >&2
	exit 1
fi
grep -Fq 'group_add_next doorfast' "$trace"
! grep -Fq 'mkdir -p /var/run/doorfast' "$trace"
! grep -Fq 'procd_open_instance' "$trace"

: >"$trace"
group_present=1
group_creation_succeeds=1
doorfast_enabled=1
doorfast_active_host=1
doorfast_indoor_ipaddr=
start_service
grep -Fxq 'ip link set dev host0 up' "$trace"
grep -Fxq 'ip address add 10.5.65.0/8 dev host0' "$trace"
test "$(cat "$HOST_ADDRESS_STATE")" = 'host0 10.5.65.0/8'
stop_service
grep -Fxq 'ip address del 10.5.65.0/8 dev host0' "$trace"

: >"$trace"
doorfast_indoor_ipaddr=10.99.1.7
doorfast_indoor_netmask=255.255.255.0
start_service
grep -Fxq 'ip address add 10.99.1.7/24 dev host0' "$trace"

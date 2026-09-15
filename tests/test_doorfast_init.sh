#!/bin/sh
set -eu

trace=$(mktemp)
trap 'rm -f "$trace"' EXIT

current_config=
doorfast_enabled=0
doorfast_call_elev=1
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
	eval "$1=\$value"
}

config_get_bool() {
	value=$4
	if [ "$current_config" = doorfast ]; then
		case "$3" in
			enabled) value=$doorfast_enabled ;;
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

procd_open_instance() { printf 'procd_open_instance\n' >>"$trace"; }
procd_set_param() { :; }
procd_close_instance() { :; }
procd_add_reload_trigger() { :; }

. package/doorfast/files/doorfast.init

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

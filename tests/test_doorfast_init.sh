#!/bin/sh
set -eu

trace=$(mktemp)
trap 'rm -f "$trace"' EXIT

current_config=
doorfast_enabled=0
doorfast_call_elev=1
automation_call_elev=

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

procd_open_instance() { :; }
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

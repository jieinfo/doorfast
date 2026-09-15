#!/bin/sh
set -eu

. package/doorfast/files/doorfast-event-relay.init

mode=secure
ls() {
	case "$mode" in
		secure) printf '%s\n' '-rw------- 1 0 0 7 Sep 15 00:00 token' ;;
		group_readable) printf '%s\n' '-rw-r----- 1 0 0 7 Sep 15 00:00 token' ;;
		non_root) printf '%s\n' '-rw------- 1 1000 1000 7 Sep 15 00:00 token' ;;
	esac
}

token_file_secure /fake/token

mode=group_readable
if token_file_secure /fake/token; then
	echo 'relay init accepted a group-readable token' >&2
	exit 1
fi

mode=non_root
if token_file_secure /fake/token; then
	echo 'relay init accepted a non-root token' >&2
	exit 1
fi

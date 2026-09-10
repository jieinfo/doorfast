#!/bin/sh
set -eu
make recorder
build/doorfast-recorder --help >/dev/null
for argument in /tmp / /mnt/doorfast /tmp/doorfast-recorder-selftest; do
    if build/doorfast-recorder --config "$argument"; then
        exit 1
    else
        test "$?" -eq 2
    fi
done
started=0
deployment_enabled=0
recording=0
config_load() { test "$1" = doorfast-deployment; }
config_get_bool() {
    case "$1" in
        enabled) enabled=$deployment_enabled ;;
        recording_enabled) recording_enabled=$recording ;;
        *) return 1 ;;
    esac
}
procd_open_instance() { started=$((started + 1)); }
procd_set_param() { :; }
procd_close_instance() { :; }
. package/doorfast/files/doorfast-recorder.init
start_service
test "$started" -eq 0
deployment_enabled=1
start_service
test "$started" -eq 0
recording=1
start_service
test "$started" -eq 1
test ! -e /tmp/doorfast-recorder-selftest
mkdir -m 700 /tmp/doorfast-recorder-selftest
trap 'rmdir /tmp/doorfast-recorder-selftest' EXIT
build/doorfast-recorder --self-test /tmp/doorfast-recorder-selftest

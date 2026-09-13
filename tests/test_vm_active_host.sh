#!/bin/sh
set -eu

vm_dir=${DOORFAST_VM_DIR:-/Users/shenwenjie/Documents/PVE/vms/doorfast-immortalwrt-25.12.1-x86_64}
if [ ! -x "$vm_dir/p1-active-host-test.sh" ]; then
    printf 'Doorfast VM active_host test skipped (VM script unavailable).\n'
    exit 0
fi
(cd "$vm_dir" && ./p1-active-host-test.sh)

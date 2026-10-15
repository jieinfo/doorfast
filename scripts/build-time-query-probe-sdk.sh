#!/bin/sh
# Build a separate diagnostic artifact; never stage it into a package root.
set -eu
sdk_dir=$(CDPATH= cd -- "${1:?usage: build-time-query-probe-sdk.sh SDK OUTPUT}" && pwd)
output=${2:?usage: build-time-query-probe-sdk.sh SDK OUTPUT}
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
set -- "$sdk_dir"/staging_dir/toolchain-*/bin/*-openwrt-linux-musl-gcc
[ "$#" -eq 1 ] && [ -x "$1" ] || { echo 'unique SDK cross compiler unavailable' >&2; exit 1; }
compiler=$1
set -- "$sdk_dir"/staging_dir/target-*/usr/include
[ "$#" -eq 1 ] && [ -d "$1" ] || { echo 'unique SDK target staging unavailable' >&2; exit 1; }
target_include=$1
target_lib=$target_include/../lib
mkdir -p "$(dirname -- "$output")"
STAGING_DIR="$sdk_dir/staging_dir" "$compiler" -D_DEFAULT_SOURCE -DDF_WITH_UBUS \
    -std=c17 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$repo_dir/src" -I"$target_include" -L"$target_lib" -Wl,-rpath-link,"$target_lib" \
    "$repo_dir/tools/diagnostics/time_query_probe_main.c" \
    "$repo_dir/tools/diagnostics/time_query_probe.c" \
    "$repo_dir/src/gvs_frame.c" "$repo_dir/src/gvs_packet.c" \
    "$repo_dir/src/gvs_serialize.c" "$repo_dir/src/gvs_vendor_header.c" \
    "$repo_dir/src/runtime_id.c" -lpcap -lubus -lubox -o "$output"

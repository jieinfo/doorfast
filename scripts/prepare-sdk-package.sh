#!/bin/sh
set -eu

sdk_dir=${1:?usage: prepare-sdk-package.sh SDK_DIRECTORY}
repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
doorfast_destination="$sdk_dir/package/doorfast"
luci_destination="$sdk_dir/package/luci-app-doorfast"

test -d "$sdk_dir"
test ! -e "$doorfast_destination"
test ! -e "$luci_destination"
mkdir -p "$sdk_dir/package"
cp -R "$repo_dir/package/doorfast" "$doorfast_destination"
cp -R "$repo_dir/package/luci-app-doorfast" "$luci_destination"
cp -R "$repo_dir/src" "$doorfast_destination/src"

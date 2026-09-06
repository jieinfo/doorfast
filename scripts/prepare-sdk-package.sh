#!/bin/sh
set -eu

sdk_dir=${1:?usage: prepare-sdk-package.sh SDK_DIRECTORY}
repo_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
destination="$sdk_dir/package/doorfast"

test -d "$sdk_dir"
test ! -e "$destination"
mkdir -p "$sdk_dir/package"
cp -R "$repo_dir/package/doorfast" "$destination"
cp -R "$repo_dir/src" "$destination/src"

#!/bin/sh
set -eu

test -f package/doorfast/Makefile
test -f package/doorfast/files/doorfast.init
test -f package/doorfast/files/doorfast.config
test -f scripts/prepare-sdk-package.sh
grep -q 'PKGARCH:=x86_64' package/doorfast/Makefile
! grep -R -E -q 'wget -O-|auth|auto_update|opkg|\.ipk' package/doorfast scripts
grep -q 'scripts/feeds install libpcap libuci libjson-c libopenssl' .github/workflows/build-apk.yml
grep -q 'actions/cache@v4' .github/workflows/build-apk.yml
grep -q 'doorfast-\*.apk' .github/workflows/build-apk.yml
! grep -q 'bin/packages/\*\*/\*.apk' .github/workflows/build-apk.yml
grep -q 'config_load doorfast' package/doorfast/files/doorfast.init
grep -q 'config_get_bool enabled main enabled 0' package/doorfast/files/doorfast.init

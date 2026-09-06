#!/bin/sh
set -eu

test -f package/doorfast/Makefile
test -f package/doorfast/files/doorfast.init
test -f package/doorfast/files/doorfast.config
test -f scripts/prepare-sdk-package.sh
rg -q 'PKGARCH:=x86_64' package/doorfast/Makefile
! rg -q 'wget -O-|auth|auto_update|opkg|\.ipk' package/doorfast scripts

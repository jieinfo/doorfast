#!/bin/sh
set -eu

make doorfast
./build/doorfast --help | grep -q '^Usage: doorfast '

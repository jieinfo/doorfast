#!/bin/sh
set -eu

temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-media-migration.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

helper="$temporary_directory/migration-helper.sh"
sed -n '/^migrate_media_credentials()/,/^}/p' package/doorfast/Makefile |
    sed 's/\$\$/\$/g' >"$helper"
. "$helper"
command -v migrate_media_credentials >/dev/null

credentials="$temporary_directory/media-credentials"
expected="$temporary_directory/expected"
printf '%s\n' 'rtsp_password=keep-this' 'relay_token=remove-this' >"$credentials"
printf '%s\n' 'rtsp_password=keep-this' >"$expected"
chmod 0640 "$credentials"
before_owner_group=$(ls -ln "$credentials" | awk '{print $3 ":" $4}')

migrate_media_credentials "$credentials" >/dev/null
cmp "$expected" "$credentials"
test "$(stat -f '%Lp' "$credentials")" = '600'
test "$(ls -ln "$credentials" | awk '{print $3 ":" $4}')" = "$before_owner_group"
! find "$temporary_directory" -name 'media-credentials.tmp.*' -print -quit | grep .

printf '%s\n' 'rtsp_password=still-here' 'relay_token=still-remove' >"$credentials"
mkdir "$temporary_directory/bin"
printf '%s\n' '#!/bin/sh' 'exit 1' >"$temporary_directory/bin/mv"
chmod 0755 "$temporary_directory/bin/mv"
if PATH="$temporary_directory/bin:$PATH" \
    migrate_media_credentials "$credentials" >"$temporary_directory/stdout" 2>"$temporary_directory/stderr"
then
    exit 1
fi
grep -Fxq 'rtsp_password=still-here' "$credentials"
grep -Fxq 'relay_token=still-remove' "$credentials"
! grep -Fq 'still-here' "$temporary_directory/stdout"
! grep -Fq 'still-remove' "$temporary_directory/stdout"
! grep -Fq 'still-here' "$temporary_directory/stderr"
! grep -Fq 'still-remove' "$temporary_directory/stderr"
! find "$temporary_directory" -name 'media-credentials.tmp.*' -print -quit | grep .

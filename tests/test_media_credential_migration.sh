#!/bin/sh
set -eu

temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-media-migration.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

mode() {
	if stat -c %a "$1" >/dev/null 2>&1; then
		stat -c %a "$1"
	else
		stat -f %Lp "$1"
	fi
}

assert_no_secrets() {
	for stream in "$@"; do
		! grep -Fq 'keep-this' "$stream"
		! grep -Fq 'remove-this' "$stream"
		! grep -Fq 'still-here' "$stream"
		! grep -Fq 'still-remove' "$stream"
	done
}

assert_no_temporary_file() {
	! find "$temporary_directory" -name 'media-credentials.tmp.*' -print -quit |
		grep .
}

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

migrate_media_credentials "$credentials" \
	>"$temporary_directory/success.stdout" \
	2>"$temporary_directory/success.stderr"
cmp "$expected" "$credentials"
test "$(mode "$credentials")" = '600'
test "$(ls -ln "$credentials" | awk '{print $3 ":" $4}')" = "$before_owner_group"
assert_no_secrets "$temporary_directory/success.stdout" \
	"$temporary_directory/success.stderr"
assert_no_temporary_file

mkdir "$temporary_directory/bin"
for failed_command in awk chmod chown mv; do
	printf '%s\n' 'rtsp_password=still-here' 'relay_token=still-remove' >"$credentials"
	cp "$credentials" "$temporary_directory/original"
	rm -f "$temporary_directory/bin/awk" "$temporary_directory/bin/chmod" \
		"$temporary_directory/bin/chown" "$temporary_directory/bin/mv"
	printf '%s\n' '#!/bin/sh' 'exit 1' >"$temporary_directory/bin/$failed_command"
	chmod 0755 "$temporary_directory/bin/$failed_command"
	if PATH="$temporary_directory/bin:$PATH" \
		migrate_media_credentials "$credentials" \
		>"$temporary_directory/$failed_command.stdout" \
		2>"$temporary_directory/$failed_command.stderr"
	then
		exit 1
	fi
	cmp "$temporary_directory/original" "$credentials"
	assert_no_secrets "$temporary_directory/$failed_command.stdout" \
		"$temporary_directory/$failed_command.stderr"
	assert_no_temporary_file
done

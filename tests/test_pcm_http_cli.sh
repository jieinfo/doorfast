#!/bin/sh
set -eu

make pcm-http-test >/dev/null
workspace=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-pcm-http.XXXXXXXX")
trap 'rm -rf "$workspace"' EXIT
state="$workspace/state"
sink="$workspace/sink"
stdout="$workspace/stdout"
stderr="$workspace/stderr"

run_cgi() {
	path=$1
	query=$2
	method=$3
	content_type=$4
	content_length=$5
	session=$6
	: >"$stdout"
	: >"$stderr"
	PATH_INFO="$path" QUERY_STRING="$query" REQUEST_METHOD="$method" \
		CONTENT_TYPE="$content_type" CONTENT_LENGTH="$content_length" \
		HTTP_X_DOORFAST_AUDIO_SESSION="$session" \
		DF_PCM_HTTP_TEST_STATE="$state" DF_PCM_HTTP_TEST_SINK="$sink" \
		build/doorfast-pcm-http >"$stdout" 2>"$stderr"
	grep -aFq 'Content-Type: application/json' "$stdout"
	grep -aFq 'Cache-Control: no-store' "$stdout"
}

assert_response() {
	expected_status=$1
	expected_json=$2
	tr -d '\r' <"$stdout" | grep -aFxq "Status: $expected_status"
	tail -n 1 "$stdout" | grep -Fxq "$expected_json"
	test ! -s "$stderr"
}

run_cgi /api/v1/audio/session \
	'runtime=0123456789abcdef&generation=7' GET '' 0 ''
assert_response '405 Method Not Allowed' '{"error":"method_not_allowed"}'

run_cgi /api/v1/audio/session \
	'runtime=0123456789abcdef&runtime=0123456789abcdef&generation=7' \
	POST '' 0 ''
assert_response '400 Bad Request' '{"error":"invalid_request"}'

run_cgi /api/v1/audio/session \
	'runtime=0123456789abcdef&generation=7&unknown=RAW_REQUEST_MARKER' POST '' 0 ''
assert_response '400 Bad Request' '{"error":"invalid_request"}'
! grep -aFq 'RAW_REQUEST_MARKER' "$stdout"
! grep -aFq 'RAW_REQUEST_MARKER' "$stderr"

run_cgi /api/v1/audio/session \
	'runtime=0123456789abcdef&generation=7' POST '' 1 ''
assert_response '400 Bad Request' '{"error":"invalid_request"}'

run_cgi /api/v1/audio/session \
	'runtime=fedcba9876543210&generation=7' POST '' 0 ''
assert_response '409 Conflict' \
	'{"error":"runtime_mismatch","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":0}'

run_cgi /api/v1/audio/session \
	'runtime=0123456789abcdef&generation=7' POST '' 0 ''
tr -d '\r' <"$stdout" | grep -aFxq 'Status: 200 OK'
session=$(sed -n 's/.*"audio_session":"\([0-9a-f]*\)".*/\1/p' "$stdout")
test "${#session}" -eq 32
test ! -s "$stderr"

python3 - "$workspace/body" <<'PY'
import sys
with open(sys.argv[1], "wb") as stream:
    stream.write(bytes(range(256)) + bytes(range(64)))
PY

PATH_INFO=/api/v1/audio/submit.pcm \
	QUERY_STRING='runtime=0123456789abcdef&generation=7&sequence=0' \
	REQUEST_METHOD=POST CONTENT_TYPE=application/octet-stream CONTENT_LENGTH=320 \
	HTTP_X_DOORFAST_AUDIO_SESSION="$session" \
	DF_PCM_HTTP_TEST_STATE="$state" DF_PCM_HTTP_TEST_SINK="$sink" \
	build/doorfast-pcm-http <"$workspace/body" >"$stdout" 2>"$stderr"
tr -d '\r' <"$stdout" | grep -aFxq 'Status: 200 OK'
grep -aFq 'Content-Type: application/json' "$stdout"
grep -aFq 'Cache-Control: no-store' "$stdout"
tail -n 1 "$stdout" | grep -Fxq \
	'{"status":"success","runtime_id":"0123456789abcdef","generation":7,"audio_session":"","accepted_frames":1,"next_sequence":1,"lease_ms":2000}'
cmp "$workspace/body" "$sink"
test ! -s "$stderr"
python3 - "$workspace/body" "$stdout" "$stderr" <<'PY'
import sys
body = open(sys.argv[1], "rb").read()
assert body not in open(sys.argv[2], "rb").read()
assert body not in open(sys.argv[3], "rb").read()
PY

printf 'short' | PATH_INFO=/api/v1/audio/submit.pcm \
	QUERY_STRING='runtime=0123456789abcdef&generation=7&sequence=1' \
	REQUEST_METHOD=POST CONTENT_TYPE=application/octet-stream CONTENT_LENGTH=320 \
	HTTP_X_DOORFAST_AUDIO_SESSION="$session" \
	DF_PCM_HTTP_TEST_STATE="$state" DF_PCM_HTTP_TEST_SINK="$sink" \
	build/doorfast-pcm-http >"$stdout" 2>"$stderr"
tr -d '\r' <"$stdout" | grep -aFxq 'Status: 400 Bad Request'
tail -n 1 "$stdout" | grep -Fxq \
	'{"error":"invalid_body","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":1}'
test "$(wc -c <"$sink" | tr -d ' ')" -eq 320
test ! -s "$stderr"

PATH_INFO=/api/v1/audio/submit.pcm \
	QUERY_STRING='runtime=0123456789abcdef&generation=7&sequence=1' \
	REQUEST_METHOD=POST CONTENT_TYPE=application/octet-stream CONTENT_LENGTH=320 \
	HTTP_X_DOORFAST_AUDIO_SESSION="$session" \
	DF_PCM_HTTP_TEST_STATE="$state" DF_PCM_HTTP_TEST_SINK= \
	build/doorfast-pcm-http <"$workspace/body" >"$stdout" 2>"$stderr"
tr -d '\r' <"$stdout" | grep -aFxq 'Status: 503 Service Unavailable'
tail -n 1 "$stdout" | grep -Fxq \
	'{"error":"send_unavailable","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":1}'
test ! -s "$stderr"

run_cgi /api/v1/audio/submit.pcm \
	'runtime=0123456789abcdef&generation=7&sequence=1' POST \
	application/octet-stream 320 bad
assert_response '400 Bad Request' '{"error":"invalid_request"}'

run_cgi /api/v1/audio/submit.pcm \
	'runtime=0123456789abcdef&generation=7&sequence=1' POST \
	text/plain 320 "$session"
assert_response '400 Bad Request' '{"error":"invalid_request"}'

run_cgi /api/v1/audio/submit.pcm \
	'runtime=0123456789abcdef&generation=7&sequence=1' POST \
	application/octet-stream 321 "$session"
assert_response '400 Bad Request' '{"error":"invalid_request"}'

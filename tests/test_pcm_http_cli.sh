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
	request_session=$6
	authorization=${7:--}
	request_body=${8:-/dev/null}
	: >"$stdout"
	: >"$stderr"
	(
		unset HTTP_X_DOORFAST_AUDIO_SESSION HTTP_AUTHORIZATION
		if [ "$request_session" != - ]; then
			export HTTP_X_DOORFAST_AUDIO_SESSION="$request_session"
		fi
		if [ "$authorization" != - ]; then
			export HTTP_AUTHORIZATION="$authorization"
		fi
		PATH_INFO="$path" QUERY_STRING="$query" REQUEST_METHOD="$method" \
			CONTENT_TYPE="$content_type" CONTENT_LENGTH="$content_length" \
			DF_PCM_HTTP_TEST_STATE="$state" DF_PCM_HTTP_TEST_SINK="$sink" \
			build/doorfast-pcm-http <"$request_body"
	) >"$stdout" 2>"$stderr"
	grep -aFq 'Content-Type: application/json' "$stdout"
	grep -aFq 'Cache-Control: no-store' "$stdout"
}

assert_response() {
	expected_status=$1
	expected_json=$2
	if ! tr -d '\r' <"$stdout" | grep -aFxq "Status: $expected_status"; then
		printf 'Expected CGI status: %s\n' "$expected_status" >&2
		tr -d '\r' <"$stdout" >&2
		return 1
	fi
	if ! tail -n 1 "$stdout" | grep -Fxq "$expected_json"; then
		printf 'Unexpected CGI JSON (expected %s)\n' "$expected_json" >&2
		tail -n 1 "$stdout" >&2
		return 1
	fi
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
assert_response '400 Bad Request' \
	'{"error":"invalid_request","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":1}'

run_cgi /api/v1/audio/submit.pcm \
	'runtime=0123456789abcdef&generation=7&sequence=1' POST \
	application/octet-stream 321 "$session"
assert_response '400 Bad Request' \
	'{"error":"invalid_request","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":1}'

# Format/conflict rejection must not depend on a still-live producer lease.
for auth_route in /api/v1/audio/submit.pcm /api/v1/audio/session/end; do
	auth_query='runtime=0123456789abcdef&generation=7'
	auth_length=0
	auth_body=/dev/null
	if [ "$auth_route" = /api/v1/audio/submit.pcm ]; then
		auth_query="$auth_query&sequence=1"
		auth_length=320
		auth_body="$workspace/body"
	fi
	state_before=$(cksum "$state")
	sink_before=$(cksum "$sink")
	for missing_legacy in - ''; do
		run_cgi "$auth_route" "$auth_query" POST application/octet-stream \
			"$auth_length" "$missing_legacy" - "$auth_body"
		assert_response '400 Bad Request' '{"error":"invalid_request"}'
		test "$(cksum "$state")" = "$state_before"
		test "$(cksum "$sink")" = "$sink_before"
	done
	for conflicting_legacy in '' bad 00000000000000000000000000000000; do
		run_cgi "$auth_route" "$auth_query" POST application/octet-stream \
			"$auth_length" "$conflicting_legacy" "DoorfastAudio $session" "$auth_body"
		assert_response '400 Bad Request' '{"error":"invalid_request"}'
		test "$(cksum "$state")" = "$state_before"
		test "$(cksum "$sink")" = "$sink_before"
	done
	for malformed_auth in 'DoorfastAudio' 'DoorfastAudio ' \
		"DoorfastAudio  $session" "DoorfastAudio $session " \
		'DoorfastAudio ABCDEFABCDEFABCDEFABCDEFABCDEFAB' \
		'DoorfastAudio ababababababababababababababab' \
		'DoorfastAudio ababababababababababababababababab' \
		"DoorfastAudioX $session" "Bearer $session" 'Basic dGVzdDp0ZXN0'; do
		run_cgi "$auth_route" "$auth_query" POST application/octet-stream \
			"$auth_length" - "$malformed_auth" "$auth_body"
		assert_response '400 Bad Request' '{"error":"invalid_request"}'
		test "$(cksum "$state")" = "$state_before"
		test "$(cksum "$sink")" = "$sink_before"
	done
	run_cgi "$auth_route" "$auth_query" POST application/octet-stream \
		"$auth_length" "$session" 'DoorfastAudio 00000000000000000000000000000000' "$auth_body"
	assert_response '400 Bad Request' '{"error":"invalid_request"}'
	test "$(cksum "$state")" = "$state_before"
	test "$(cksum "$sink")" = "$sink_before"
done

run_cgi /api/v1/audio/session/end \
	'runtime=fedcba9876543210&generation=7' POST '' 0 - "DoorfastAudio $session"
assert_response '409 Conflict' \
	'{"error":"runtime_mismatch","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":1}'
run_cgi /api/v1/audio/submit.pcm \
	'runtime=0123456789abcdef&generation=8&sequence=1' POST \
	application/octet-stream 320 - "DoorfastAudio $session" "$workspace/body"
assert_response '409 Conflict' \
	'{"error":"generation_mismatch","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":1}'
test "$(cksum "$state")" = "$state_before"
test "$(cksum "$sink")" = "$sink_before"

# Only the lease-dependent cases use this freshly opened, isolated state.
state="$workspace/header-state"
run_cgi /api/v1/audio/session \
	'runtime=0123456789abcdef&generation=7' POST '' 0 ''
tr -d '\r' <"$stdout" | grep -aFxq 'Status: 200 OK'
session=$(sed -n 's/.*"audio_session":"\([0-9a-f]*\)".*/\1/p' "$stdout")
test "${#session}" -eq 32
state_before=$(cksum "$state")
for auth_route in /api/v1/audio/submit.pcm /api/v1/audio/session/end; do
	auth_query='runtime=0123456789abcdef&generation=7'
	auth_length=0
	auth_body=/dev/null
	if [ "$auth_route" = /api/v1/audio/submit.pcm ]; then
		auth_query="$auth_query&sequence=0"
		auth_length=320
		auth_body="$workspace/body"
	fi
	run_cgi "$auth_route" "$auth_query" POST application/octet-stream \
		"$auth_length" - 'DoorfastAudio 00000000000000000000000000000000' "$auth_body"
	assert_response '409 Conflict' \
		'{"error":"session_mismatch","runtime_id":"0123456789abcdef","generation":7,"accepted_frames":0,"next_sequence":0}'
	test "$(cksum "$state")" = "$state_before"
	test "$(cksum "$sink")" = "$sink_before"
done

# uhttpd exports Authorization but drops the custom session header entirely.
run_cgi /api/v1/audio/submit.pcm \
	'runtime=0123456789abcdef&generation=7&sequence=0' POST \
	application/octet-stream 320 - "dOoRfAsTaUdIo $session" "$workspace/body"
assert_response '200 OK' \
	'{"status":"success","runtime_id":"0123456789abcdef","generation":7,"audio_session":"","accepted_frames":1,"next_sequence":1,"lease_ms":2000}'
test "$(wc -c <"$sink" | tr -d ' ')" -eq 640

run_cgi /api/v1/audio/session/end \
	'runtime=0123456789abcdef&generation=7' POST '' 0 - "DoorfastAudio $session"
assert_response '200 OK' \
	'{"status":"success","runtime_id":"0123456789abcdef","generation":7,"audio_session":"","accepted_frames":0,"next_sequence":1,"lease_ms":0}'

# The new HA client sends matching headers; old clients may use HTTP Basic
# independently of their custom audio token. Neither form changes the PCM body.
run_cgi /api/v1/audio/session \
	'runtime=0123456789abcdef&generation=7' POST '' 0 ''
tr -d '\r' <"$stdout" | grep -aFxq 'Status: 200 OK'
for compatible_auth in "DoorfastAudio $session" 'Basic dGVzdDp0ZXN0'; do
	compatible_sequence=1
	if [ "$compatible_auth" = 'Basic dGVzdDp0ZXN0' ]; then
		compatible_sequence=2
		run_cgi /api/v1/audio/session \
			'runtime=0123456789abcdef&generation=7' POST '' 0 ''
		tr -d '\r' <"$stdout" | grep -aFxq 'Status: 200 OK'
	fi
	run_cgi /api/v1/audio/submit.pcm \
		"runtime=0123456789abcdef&generation=7&sequence=$compatible_sequence" POST \
		application/octet-stream 320 "$session" "$compatible_auth" "$workspace/body"
	tr -d '\r' <"$stdout" | grep -aFxq 'Status: 200 OK'
	test ! -s "$stderr"
	run_cgi /api/v1/audio/session/end \
		'runtime=0123456789abcdef&generation=7' POST '' 0 "$session" "$compatible_auth"
	tr -d '\r' <"$stdout" | grep -aFxq 'Status: 200 OK'
	test ! -s "$stderr"
done
test "$(wc -c <"$sink" | tr -d ' ')" -eq 1280
python3 - "$workspace/body" "$sink" <<'PY'
import sys
with open(sys.argv[1], "rb") as source, open(sys.argv[2], "rb") as sink:
    assert sink.read() == source.read() * 4, "credential handling altered PCM bytes"
PY

printf 'PCM Authorization submit/end, conflict and legacy compatibility checks passed\n'

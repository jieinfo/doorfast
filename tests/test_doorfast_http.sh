#!/bin/sh
set -eu
workspace=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-http.XXXXXXXX")
trap 'rm -rf "$workspace"' EXIT
fakebin="$workspace/bin"; trace="$workspace/trace"; mkdir -p "$fakebin"
cat >"$fakebin/ubus" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$DOORFAST_HTTP_TRACE"
method="${3:-}"
if [ "${TEST_UBUS_FAIL_METHOD:-}" = "$method" ]; then
  exit "${TEST_UBUS_FAIL_CODE:-1}"
fi
if [ "${TEST_UBUS_EMPTY_METHOD:-}" = "$method" ]; then
  exit 0
fi
if [ "${TEST_UBUS_INVALID_METHOD:-}" = "$method" ]; then
  printf '%s\n' 'not-json'
  exit 0
fi
if [ -n "${TEST_UBUS_ERROR_CODE:-}" ] && [ "$method" = "${TEST_UBUS_ERROR_METHOD:-monitor_start}" ]; then
  if [ "${TEST_UBUS_ERROR_CODE}" = capacity_busy ]; then
    printf '%s\n' '{"error":{"code":"capacity_busy","configured_capacity":2,"effective_capacity":1,"active_encoders":1}}'
  else
    printf '{"error":{"code":"%s"}}\n' "${TEST_UBUS_ERROR_CODE}"
  fi
  exit 0
fi
if [ "$*" = 'call doorfast status' ]; then
  if [ -n "${TEST_STATUS_JSON:-}" ]; then
    printf '%s\n' "$TEST_STATUS_JSON"
    exit 0
  fi
  if [ -n "${TEST_AUDIO_CHANGE_FILE:-}" ] && [ -f "$TEST_AUDIO_CHANGE_FILE" ]; then
    . "$TEST_AUDIO_CHANGE_FILE"
  fi
  printf '{"runtime_id":"%s","call":{"station_id":"%s","session":"%s","generation":%s},"video":{"ready":%s,"generation":%s,"frame_no":%s,"bytes":%s},"audio":{"pcm_stream_version":%s,"snapshot_timestamp_ms":%s,"last_timestamp_ms":%s,"snapshot_discontinuity":%s,"snapshot_ready":%s,"generation":%s,"snapshot_packet_count":%s,"snapshot_previous_packet_count":%s,"snapshot_bytes":%s,"snapshot_dropped_bytes":%s}}\n' \
    "${TEST_AUDIO_RUNTIME:-0123456789abcdef}" "${TEST_AUDIO_STATION:-gate_main}" \
    "${TEST_AUDIO_SESSION:-talking}" "${TEST_AUDIO_CALL_GENERATION:-${TEST_CALL_GENERATION:-7}}" "${TEST_VIDEO_READY:-true}" \
    "${TEST_VIDEO_GENERATION:-7}" "${TEST_VIDEO_FRAME:-12}" \
    "${TEST_VIDEO_BYTES:-4}" "${TEST_AUDIO_VERSION:-1}" \
    "${TEST_AUDIO_TIMESTAMP:-1200}" "${TEST_AUDIO_LAST_TIMESTAMP:-900}" \
    "${TEST_AUDIO_DISCONTINUITY:-false}" "${TEST_AUDIO_READY:-true}" \
    "${TEST_AUDIO_GENERATION:-7}" "${TEST_AUDIO_REVISION:-40}" \
    "${TEST_AUDIO_PREVIOUS_REVISION:-30}" "${TEST_AUDIO_BYTES:-12}" \
    "${TEST_AUDIO_DROPPED_BYTES:-0}"
elif [ "$*" = 'call doorfast stations' ]; then
  printf '%s\n' '{"runtime_id":"0123456789abcdef","revision":1,"stations":[{"id":"gate_main","name":"Main Gate","logical_address":"32:02:01:00:02:00","enabled":true,"stream_name":"doorfast_gate_main","route_source":"none","route_fresh":false,"monitorable":false,"last_seen_ms":null}]}'
else
  case "$method" in
    monitor_start)
      printf '{"state":"queued","runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7}\n' ;;
    monitor_stop)
      printf '{"state":"stopping","runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7}\n' ;;
    monitor_viewer)
      printf '{"state":"queued","runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7,"active":true}\n' ;;
    *) printf '{"ok":true}\n' ;;
  esac
fi
EOF
cat >"$fakebin/jsonfilter" <<'EOF'
#!/bin/sh
while [ "$#" -gt 0 ]; do
  if [ "$1" = -e ]; then
    expression="$2"; shift 2
  elif [ "$1" = -s ]; then
    source="$2"; shift 2
  else
    shift
  fi
done
case "$expression" in
  '@.call.'*|'@.video.'*|'@.audio.'*)
    python3 -c 'import json,sys
value=json.loads(sys.argv[1])
for key in sys.argv[2][2:].split("."): value=value[key]
print(str(value).lower() if isinstance(value,bool) else value)' "$source" "$expression"
    ;;
  '@.operation_id')
    python3 -c 'import json,sys; print(json.loads(sys.argv[1])["operation_id"])' "$source" ;;
  '@.generation')
    printf '%s' "${source:-}" |
      sed -n 's/.*"generation"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p'
    ;;
  '@.runtime_id')
    printf '%s' "${source:-}" |
      sed -n 's/.*"runtime_id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
    ;;
  '@.station_id')
    printf '%s' "${source:-}" |
      sed -n 's/.*"station_id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
    ;;
  '@.error.code') printf '%s\n' "${TEST_UBUS_ERROR_CODE:-}" ;;
  '@')
    case "${source:-}" in
      \{*\}) printf '%s' "$source" ;;
      *) exit 1 ;;
    esac
    ;;
  '@.active')
    printf '%s' "${source:-}" |
      sed -n 's/.*"active"[[:space:]]*:[[:space:]]*\([^,}]*\).*/\1/p'
    ;;
  *) exit 1 ;;
esac
EOF
chmod +x "$fakebin/ubus" "$fakebin/jsonfilter"

test_script="$workspace/doorfast-http.sh"
helper="$workspace/pcm-http-helper"
sed "s|^helper=/usr/sbin/doorfast-pcm-http$|helper=$helper|" \
  package/doorfast/files/doorfast-http.sh >"$test_script"
cat >"$helper" <<'EOF'
#!/bin/sh
printf '%s\n' "$PATH_INFO" >"$DOORFAST_PCM_HTTP_PATH"
cat >"$DOORFAST_PCM_HTTP_STDIN"
EOF
chmod +x "$test_script" "$helper"
for audio_path in \
  /api/v1/audio/session \
  /api/v1/audio/submit.pcm \
  /api/v1/audio/session/end
do
  python3 - <<'PY' >"$workspace/audio-body"
import sys
sys.stdout.buffer.write(b"pcm\0body")
PY
  PATH_INFO="$audio_path" CONTENT_LENGTH=8 \
    DOORFAST_PCM_HTTP_PATH="$workspace/audio-path" \
    DOORFAST_PCM_HTTP_STDIN="$workspace/audio-stdin" \
    sh "$test_script" <"$workspace/audio-body" >"$workspace/audio-output"
  grep -Fxq "$audio_path" "$workspace/audio-path"
  cmp "$workspace/audio-body" "$workspace/audio-stdin"
  test ! -s "$workspace/audio-output"
done
rm -f "$workspace/audio-path" "$workspace/audio-stdin"
PATH_INFO=/api/v1/audio/session/ DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_PCM_HTTP_PATH="$workspace/audio-path" \
  DOORFAST_PCM_HTTP_STDIN="$workspace/audio-stdin" \
  sh "$test_script" </dev/null >/dev/null
test ! -e "$workspace/audio-path"
test ! -e "$workspace/audio-stdin"
run() {
  (export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" PATH_INFO="$1" CONTENT_LENGTH="${#2}"; printf '%s' "$2" | sh package/doorfast/files/doorfast-http.sh >/dev/null)
}
run_method() {
  (export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
    PATH_INFO="$1" REQUEST_METHOD="$3" CONTENT_TYPE=application/json \
    CONTENT_LENGTH="${#2}"; \
    printf '%s' "$2" | sh package/doorfast/files/doorfast-http.sh)
}
run /api/v1/status ''
# Explicit operation ownership must survive the real CGI request bridge.
identity='"runtime_id":"0123456789abcdef","station_id":"gate_main"'
run_method /api/v1/hangup "{$identity,\"operation_id\":18446744073709551615}" POST >"$workspace/operation"
if ! tail -n 1 "$trace" | grep -Fq '"operation_id":18446744073709551615'; then
  printf '%s\n' 'FAIL: station hangup discarded operation_id'
  exit 1
fi
for invalid in '"1"' 'null' 'true' '-1' '0' '1.5' '1e2' '18446744073709551616' '1,"operation_id":2'; do
  before=$(wc -l <"$trace")
  run_method /api/v1/hangup "{$identity,\"operation_id\":$invalid}" POST >"$workspace/operation"
  grep -aFq 'Status: 400 Bad Request' "$workspace/operation"
  test "$before" -eq "$(wc -l <"$trace")"
done
for invalid in \
  '{"runtime_id":"0123456789abcdef","station\u005fid":"gate_main","operation\u005fid":18446744073709551616,"generation":7}' \
  '{"runtime_id":"0123456789abcdef","station\u005fid":"gate_main","station\u005fid":"gate_side","generation":7}' \
  '{"runtime_id":"0123456789abcdef","operation\u005fid":-1,"generation":7}' \
  '{"runtime_id":"0123456789abcdef","station\u005fid":"gate_main","secret\u005fkey":"do-not-echo","generation":7}'
do
  before=$(wc -l <"$trace")
  run_method /api/v1/hangup "$invalid" POST >"$workspace/operation"
  if ! grep -aFq 'Status: 400 Bad Request' "$workspace/operation"; then
    printf '%s\n' 'FAIL: escaped station hangup key bypassed validation'
    exit 1
  fi
  test "$before" -eq "$(wc -l <"$trace")"
  ! grep -aFq 'do-not-echo' "$workspace/operation"
done
for invalid in "{$identity,\"station_id\":\"gate_side\"}" "{$identity,\"secret\":\"do-not-echo\"}"; do
  before=$(wc -l <"$trace")
  run_method /api/v1/call "$invalid" POST >"$workspace/operation"
  grep -aFq 'Status: 400 Bad Request' "$workspace/operation"
  ! grep -aFq 'do-not-echo' "$workspace/operation"
  test "$before" -eq "$(wc -l <"$trace")"
done
(TEST_UBUS_ERROR_CODE=operation_mismatch TEST_UBUS_ERROR_METHOD=hangup \
  run_method /api/v1/hangup "{$identity,\"operation_id\":1}" POST) >"$workspace/operation"
grep -aFq 'Status: 409 Conflict' "$workspace/operation"
printf '%s\n' 'PASS: station call identity validation and operation forwarding'
# The bridge already passes status through unchanged. These fixtures must stay
# GREEN without a CGI whitelist or a new interpretation of diagnostic fields.
for status_fixture in \
  '{"running":true,"call":{"session":"talking","generation":9,"timer":"confirmation","remaining_ms":500,"end_reason":"none","fault":"confirmation_timeout","last_station_id":"gate_side","last_generation":8},"media":{"sessions":[{"station_id":"gate_main","generation":9,"timer":"first_frame","remaining_ms":900,"end_reason":"none","failure_reason":"none"}],"last_results":[{"station_id":"gate_main","generation":8,"timer":"none","remaining_ms":0,"end_reason":"local_hangup","failure_reason":"cancelled"}]}}' \
  '{"running":true,"call":{"session":"talking","generation":9},"media":{"sessions":[{"station_id":"gate_main","generation":9,"control_requests":3}]}}' \
  '{"running":true,"call":{"timer":"unknown","end_reason":"unknown","fault":"unknown"},"media":{"sessions":[{"station_id":"gate_main","generation":9,"timer":"unknown","end_reason":"unknown","failure_reason":"unknown"}],"last_results":[]}}'
do
  (TEST_STATUS_JSON="$status_fixture" run_method /api/v1/status '' GET) >"$workspace/status-fixture"
  TEST_STATUS_JSON="$status_fixture" python3 - "$workspace/status-fixture" <<'PY'
import json, os, sys
response = open(sys.argv[1]).read()
actual = json.loads(response.split('\n\n', 1)[1])
assert actual == json.loads(os.environ['TEST_STATUS_JSON'])
assert not any(key in response for key in (
    'access_material', 'password=', 'authorization', 'bearer=', 'pcm=', 'sdp', 'secretkey'))
if 'last_results' in actual['media'] and actual['media']['last_results']:
    current = actual['media']['sessions'][0]
    last = actual['media']['last_results'][0]
    assert current['generation'] == 9 and last['generation'] == 8
    assert current['remaining_ms'] == 900 and last['remaining_ms'] == 0
PY
done
run /api/v1/unlock '{"station_id":"gate_main"}'
run /api/v1/answer '{"runtime_id":"0123456789abcdef","generation":7,"primary_media_port":8303}'
run_method /api/v1/hangup \
  '{"runtime_id":"0123456789abcdef","generation":7,"reason":"ha"}' POST \
  >/dev/null
run_method /api/v1/call \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main"}' POST \
  >"$workspace/call"
tr -d '\r' <"$workspace/call" >"$workspace/call-normalized"
grep -Eq '^Content-Type: application/json$' "$workspace/call-normalized"
test "$(grep -c '^Content-Type:' "$workspace/call-normalized")" -eq 1
awk 'BEGIN { body = 0 } /^$/ { body = 1; next } body { print }' \
  "$workspace/call-normalized" >"$workspace/call-body"
grep -Eq '^\{.*\}$' "$workspace/call-body"
! grep -Fq 'Content-Type:' "$workspace/call-body"
(
  export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
    TEST_UBUS_FAIL_METHOD=call PATH_INFO=/api/v1/call \
    REQUEST_METHOD=POST CONTENT_TYPE=application/json \
    CONTENT_LENGTH=58
  printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main"}' |
    sh package/doorfast/files/doorfast-http.sh >"$workspace/call-failed"
)
grep -aFq 'Status: 503 Service Unavailable' "$workspace/call-failed"
grep -aFq '"code":"service_unavailable"' "$workspace/call-failed"
(
  export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
    TEST_UBUS_EMPTY_METHOD=hangup PATH_INFO=/api/v1/hangup \
    REQUEST_METHOD=POST CONTENT_TYPE=application/json \
    CONTENT_LENGTH=58
  printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main"}' |
    sh package/doorfast/files/doorfast-http.sh >"$workspace/hangup-empty"
)
grep -aFq 'Status: 503 Service Unavailable' "$workspace/hangup-empty"
grep -aFq '"code":"service_unavailable"' "$workspace/hangup-empty"
run /api/v1/call_elevator '{"runtime_id":"0123456789abcdef","direction":"up"}'
run_method /api/v1/stations '' GET >"$workspace/stations"
grep -aFq 'Content-Type: application/json' "$workspace/stations"
grep -aFq 'Cache-Control: no-store' "$workspace/stations"
tail -n 1 "$workspace/stations" >"$workspace/stations-json"
printf '%s\n' '{"runtime_id":"0123456789abcdef","revision":1,"stations":[{"id":"gate_main","name":"Main Gate","logical_address":"32:02:01:00:02:00","enabled":true,"stream_name":"doorfast_gate_main","route_source":"none","route_fresh":false,"monitorable":false,"last_seen_ms":null}]}' \
  >"$workspace/stations-expected"
cmp "$workspace/stations-expected" "$workspace/stations-json"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_EMPTY_METHOD=stations PATH_INFO=/api/v1/stations \
  REQUEST_METHOD=GET CONTENT_LENGTH=0; \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/stations-empty")
grep -aFq 'Status: 503 Service Unavailable' "$workspace/stations-empty"
grep -aFq '"code":"service_unavailable","message":"station service unavailable"' \
  "$workspace/stations-empty"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_INVALID_METHOD=stations PATH_INFO=/api/v1/stations \
  REQUEST_METHOD=GET CONTENT_LENGTH=0; \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/stations-invalid-json")
grep -aFq 'Status: 503 Service Unavailable' "$workspace/stations-invalid-json"
grep -aFq '"code":"service_unavailable","message":"station service unavailable"' \
  "$workspace/stations-invalid-json"
run_method /api/v1/monitor/start \
  '{"station_id":"gate_main","runtime_id":"0123456789abcdef"}' POST >"$workspace/monitor-start"
run_method /api/v1/monitor/stop \
  '{"generation":7,"station_id":"gate_main","runtime_id":"0123456789abcdef"}' POST >"$workspace/monitor-stop"
run_method /api/v1/monitor/viewer \
  '{"active":true,"generation":7,"station_id":"gate_main","runtime_id":"0123456789abcdef"}' POST \
  >"$workspace/monitor-viewer"
run_method /api/v1/monitor/status '' GET >"$workspace/monitor-status"
test "$(wc -l <"$trace" | tr -d ' ')" -eq 20
grep -Fxq 'call doorfast stations' "$trace"
grep -Fxq 'call doorfast monitor_start {"runtime_id":"0123456789abcdef","station_id":"gate_main"}' "$trace"
grep -Fxq 'call doorfast monitor_stop {"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7}' "$trace"
grep -Fxq 'call doorfast monitor_viewer {"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7,"active":true}' "$trace"
grep -Fxq 'call doorfast monitor_status {}' "$trace"
! grep -Fq 'secret-' "$trace"
trace_lines="$(wc -l <"$trace" | tr -d ' ')"
run_method /api/v1/stations '' POST >"$workspace/stations-post"
grep -aFq 'Status: 405 Method Not Allowed' "$workspace/stations-post"
run_method /api/v1/stations '{}' GET >"$workspace/stations-body"
grep -aFq 'Status: 400 Bad Request' "$workspace/stations-body"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  PATH_INFO=/api/v1/stations REQUEST_METHOD=GET QUERY_STRING=refresh \
  CONTENT_LENGTH=0 sh package/doorfast/files/doorfast-http.sh \
  >"$workspace/stations-query"
grep -aFq 'Status: 400 Bad Request' "$workspace/stations-query"
test "$(wc -l <"$trace" | tr -d ' ')" -eq "$trace_lines"

run_method /api/v1/monitor/stop \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7,"ignored":"secret-stop"}' POST \
  >"$workspace/monitor-extra"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-extra"
run_method /api/v1/monitor/viewer \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7,"active":true,"ignored":"secret-viewer"}' POST \
  >"$workspace/monitor-viewer-extra"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-viewer-extra"
run_method /api/v1/monitor/stop '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":0}' POST \
  >"$workspace/monitor-invalid"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-invalid"
run_method /api/v1/monitor/start '' GET >"$workspace/monitor-wrong-method"
grep -aFq 'Status: 405 Method Not Allowed' "$workspace/monitor-wrong-method"
run_method /api/v1/monitor/viewer \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7,"active":1}' POST >"$workspace/monitor-invalid-active"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-invalid-active"
run_method /api/v1/monitor/viewer \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7}' POST >"$workspace/monitor-missing-active"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-missing-active"
run_method /api/v1/monitor/stop \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":"7"}' POST >"$workspace/monitor-quoted-generation"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-quoted-generation"
run_method /api/v1/monitor/viewer \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7,"active":"true"}' POST >"$workspace/monitor-quoted-active"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-quoted-active"
run_method /api/v1/monitor/start \
  '{"runtime_id":"0123456789abcdef","station_id":"Gate-bad"}' POST >"$workspace/monitor-invalid-station"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-invalid-station"
run_method /api/v1/monitor/start \
  '{"runtime_id":"0123456789abcdef","station_id":"gate main"}' POST >"$workspace/monitor-spaced-station"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-spaced-station"
run_method /api/v1/monitor/start \
  '{"runtime_id":"0123456789abcdef","station_id":"gate_main","station_id":"gate_side"}' POST >"$workspace/monitor-duplicate-station"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-duplicate-station"
run_method /api/v1/monitor/status '' POST >"$workspace/monitor-status-wrong-method"
grep -aFq 'Status: 405 Method Not Allowed' "$workspace/monitor-status-wrong-method"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  PATH_INFO=/api/v1/monitor/start REQUEST_METHOD=POST CONTENT_TYPE=text/plain \
  CONTENT_LENGTH=0; sh package/doorfast/files/doorfast-http.sh </dev/null \
    >"$workspace/monitor-wrong-content-type")
grep -aFq 'Status: 415 Unsupported Media Type' \
  "$workspace/monitor-wrong-content-type"
test "$(wc -l <"$trace" | tr -d ' ')" -eq "$trace_lines"
! grep -F 'secret-' "$workspace"/monitor-* "$trace"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_EMPTY_METHOD=monitor_status \
  PATH_INFO=/api/v1/monitor/status REQUEST_METHOD=GET CONTENT_LENGTH=0; \
  sh package/doorfast/files/doorfast-http.sh </dev/null \
    >"$workspace/monitor-empty-success")
grep -aFq 'Content-Type: application/json' "$workspace/monitor-empty-success"
grep -aFq 'Status: 503 Service Unavailable' "$workspace/monitor-empty-success"
grep -aFq '"code":"service_unavailable","message":"monitor service unavailable"' \
  "$workspace/monitor-empty-success"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_INVALID_METHOD=monitor_status \
  PATH_INFO=/api/v1/monitor/status REQUEST_METHOD=GET CONTENT_LENGTH=0; \
  sh package/doorfast/files/doorfast-http.sh </dev/null \
    >"$workspace/monitor-invalid-json")
grep -aFq 'Status: 503 Service Unavailable' "$workspace/monitor-invalid-json"
grep -aFq '"code":"service_unavailable","message":"monitor service unavailable"' \
  "$workspace/monitor-invalid-json"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_stop TEST_UBUS_FAIL_CODE=2 \
  PATH_INFO=/api/v1/monitor/stop REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=73; \
  printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7}' | sh package/doorfast/files/doorfast-http.sh \
    >"$workspace/monitor-invalid-ubus")
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-invalid-ubus"
grep -aFq '"code":"invalid_request","message":"invalid monitor request"' \
  "$workspace/monitor-invalid-ubus"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_stop TEST_UBUS_FAIL_CODE=4 \
  PATH_INFO=/api/v1/monitor/stop REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=73; \
  printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7}' | sh package/doorfast/files/doorfast-http.sh \
    >"$workspace/monitor-generation-conflict")
grep -aFq 'Status: 503 Service Unavailable' "$workspace/monitor-generation-conflict"
grep -aFq '"code":"service_unavailable","message":"monitor service unavailable"' \
  "$workspace/monitor-generation-conflict"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_start TEST_UBUS_FAIL_CODE=8 \
  PATH_INFO=/api/v1/monitor/start REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=58; \
  printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main"}' | \
    sh package/doorfast/files/doorfast-http.sh \
    >"$workspace/monitor-state-conflict")
grep -aFq 'Status: 503 Service Unavailable' "$workspace/monitor-state-conflict"
grep -aFq '"code":"service_unavailable","message":"monitor service unavailable"' \
  "$workspace/monitor-state-conflict"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_stop TEST_UBUS_FAIL_CODE=1 \
  PATH_INFO=/api/v1/monitor/stop REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=73; \
  printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main","generation":7}' | sh package/doorfast/files/doorfast-http.sh \
    >"$workspace/monitor-unavailable")
grep -aFq 'Status: 503 Service Unavailable' "$workspace/monitor-unavailable"
for error_code in runtime_mismatch station_not_found generation_mismatch resource_exhausted; do
  (export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
    TEST_UBUS_ERROR_CODE="$error_code" PATH_INFO=/api/v1/monitor/start \
    REQUEST_METHOD=POST CONTENT_TYPE=application/json CONTENT_LENGTH=58; \
    printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main"}' | \
      sh package/doorfast/files/doorfast-http.sh >"$workspace/error-$error_code")
  grep -aFq "\"code\":\"$error_code\"" "$workspace/error-$error_code"
done
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_ERROR_CODE=capacity_busy PATH_INFO=/api/v1/monitor/start \
  REQUEST_METHOD=POST CONTENT_TYPE=application/json CONTENT_LENGTH=58; \
  printf '%s' '{"runtime_id":"0123456789abcdef","station_id":"gate_main"}' | \
    sh package/doorfast/files/doorfast-http.sh >"$workspace/error-capacity")
grep -aFq '"configured_capacity":2' "$workspace/error-capacity"
printf '\377\330\377\331' >"$workspace/latest.jpg"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_VIDEO_SNAPSHOT="$workspace/latest.jpg" \
  PATH_INFO=/api/v1/video/latest.jpg \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/video-response"
grep -aFq 'Content-Type: image/jpeg' "$workspace/video-response"
grep -aFq 'Content-Length: 4' "$workspace/video-response"
grep -aFq 'Cache-Control: no-cache' "$workspace/video-response"
grep -aFq 'ETag: "df-7-12"' "$workspace/video-response"
grep -aFq 'X-Doorfast-Generation: 7' "$workspace/video-response"
grep -aFq 'X-Doorfast-Frame: 12' "$workspace/video-response"
tail -c 4 "$workspace/video-response" | cmp - "$workspace/latest.jpg"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_VIDEO_SNAPSHOT="$workspace/latest.jpg" \
  PATH_INFO=/api/v1/video/latest.jpg QUERY_STRING=generation=8 \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/video-mismatch"
grep -aFq 'Status: 409 Conflict' "$workspace/video-mismatch"
grep -aFq '"generation":7' "$workspace/video-mismatch"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_VIDEO_SNAPSHOT="$workspace/latest.jpg" \
  PATH_INFO=/api/v1/video/latest.jpg QUERY_STRING=generation=bad \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/video-invalid"
grep -aFq 'Status: 400 Bad Request' "$workspace/video-invalid"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_VIDEO_SNAPSHOT="$workspace/latest.jpg" \
  PATH_INFO=/api/v1/video/latest.jpg HTTP_IF_NONE_MATCH='"df-7-12"' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/video-not-modified"
grep -aFq 'Status: 304 Not Modified' "$workspace/video-not-modified"
test "$(wc -c <"$workspace/video-not-modified" | tr -d ' ')" -lt 150
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_VIDEO_SNAPSHOT="$workspace/latest.jpg" TEST_VIDEO_READY=false \
  PATH_INFO=/api/v1/video/latest.jpg \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/video-unavailable"
grep -aFq 'Status: 404 Not Found' "$workspace/video-unavailable"
printf 'RIFFtestWAVE' >"$workspace/latest.wav"
mkdir "$workspace/audio-chunks"
printf 'RIFFold-WAVE' >"$workspace/audio-chunks/chunk-7-20-30-12-0.wav"
printf 'RIFFnew-WAVE' >"$workspace/audio-chunks/chunk-7-30-40-12-0.wav"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  PATH_INFO=/api/v1/audio/latest.wav \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-response"
grep -aFq 'Content-Type: audio/wav' "$workspace/audio-response"
grep -aFq 'Content-Length: 12' "$workspace/audio-response"
grep -aFq 'ETag: "df-audio-7-40"' "$workspace/audio-response"
grep -aFq 'X-Doorfast-Generation: 7' "$workspace/audio-response"
grep -aFq 'X-Doorfast-Audio-Previous-Revision: 30' "$workspace/audio-response"
grep -aFq 'X-Doorfast-Audio-Revision: 40' "$workspace/audio-response"
grep -aFq 'X-Doorfast-Audio-Dropped-Bytes: 0' "$workspace/audio-response"
tail -c 12 "$workspace/audio-response" | cmp - \
  "$workspace/audio-chunks/chunk-7-30-40-12-0.wav"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  PATH_INFO=/api/v1/audio/latest.wav QUERY_STRING='generation=7&after=30' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-cursor-next"
grep -aFq 'X-Doorfast-Audio-Revision: 40' "$workspace/audio-cursor-next"
tail -c 12 "$workspace/audio-cursor-next" | cmp - \
  "$workspace/audio-chunks/chunk-7-30-40-12-0.wav"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  PATH_INFO=/api/v1/audio/latest.wav QUERY_STRING='after=30' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-cursor-no-generation"
grep -aFq 'Status: 400 Bad Request' "$workspace/audio-cursor-no-generation"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  PATH_INFO=/api/v1/audio/latest.wav QUERY_STRING=generation=8 \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-mismatch"
grep -aFq 'Status: 409 Conflict' "$workspace/audio-mismatch"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  PATH_INFO=/api/v1/audio/latest.wav HTTP_IF_NONE_MATCH='"df-audio-7-40"' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-not-modified"
grep -aFq 'Status: 304 Not Modified' "$workspace/audio-not-modified"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  PATH_INFO=/api/v1/audio/latest.wav QUERY_STRING='generation=7&after=40' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-cursor-current"
grep -aFq 'Status: 304 Not Modified' "$workspace/audio-cursor-current"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  PATH_INFO=/api/v1/audio/latest.wav QUERY_STRING='generation=7&after=20' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-cursor-missed"
grep -aFq 'X-Doorfast-Audio-Previous-Revision: 20' "$workspace/audio-cursor-missed"
grep -aFq 'X-Doorfast-Audio-Revision: 30' "$workspace/audio-cursor-missed"
tail -c 12 "$workspace/audio-cursor-missed" | cmp - \
  "$workspace/audio-chunks/chunk-7-20-30-12-0.wav"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  PATH_INFO=/api/v1/audio/latest.wav QUERY_STRING='generation=7&after=20' \
  HTTP_IF_NONE_MATCH='"df-audio-7-40"' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-cursor-etag"
grep -aFq 'X-Doorfast-Audio-Revision: 30' "$workspace/audio-cursor-etag"
tail -c 12 "$workspace/audio-cursor-etag" | cmp - \
  "$workspace/audio-chunks/chunk-7-20-30-12-0.wav"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_SNAPSHOT="$workspace/latest.wav" TEST_AUDIO_READY=false \
  PATH_INFO=/api/v1/audio/latest.wav \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-unavailable"
grep -aFq 'Status: 404 Not Found' "$workspace/audio-unavailable"
! grep -aFq '"code":' "$workspace/audio-unavailable"
grep -Fxq 'call doorfast status' "$trace"
grep -Fxq 'call doorfast unlock {"station_id":"gate_main"}' "$trace"
grep -Fxq 'call doorfast answer {"runtime_id":"0123456789abcdef","generation":7,"primary_media_port":8303}' "$trace"
grep -Fxq 'call doorfast hangup {"runtime_id":"0123456789abcdef","generation":7,"reason":"ha"}' "$trace"
grep -Fxq 'call doorfast call_elevator {"runtime_id":"0123456789abcdef","direction":"up"}' "$trace"
trace_lines="$(wc -l <"$trace" | tr -d ' ')"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  PATH_INFO=/api/v1/unknown sh package/doorfast/files/doorfast-http.sh \
  >"$workspace/unknown"
grep -aFq 'Status: 404 Not Found' "$workspace/unknown"
test "$(wc -l <"$trace" | tr -d ' ')" -eq "$trace_lines"
# Exercise the real CGI response: omitting identity checks/metadata, accepting
# overflowing integers, or serving a replaced call must fail these assertions.
cat >"$fakebin/cp" <<'EOF'
#!/bin/sh
[ "${TEST_AUDIO_COPY_FAIL:-0}" != 1 ] || exit 1
/bin/cp "$@" || exit 1
if [ -n "${TEST_AUDIO_COPY_CHANGE:-}" ]; then
  printf '%s\n' "$TEST_AUDIO_COPY_CHANGE" >"$TEST_AUDIO_CHANGE_FILE"
fi
EOF
chmod +x "$fakebin/cp"
cat >"$fakebin/mktemp" <<'EOF'
#!/bin/sh
[ "${TEST_AUDIO_TEMP_FAIL:-0}" != 1 ] || exit 1
temporary=$(/usr/bin/mktemp "$@") || exit 1
[ -z "${TEST_AUDIO_TEMP_PATH:-}" ] || printf '%s\n' "$temporary" >"$TEST_AUDIO_TEMP_PATH"
printf '%s\n' "$temporary"
EOF
chmod +x "$fakebin/mktemp"
mkdir "$workspace/audio-v1-chunks"
python3 - <<'PY' >"$workspace/audio-v1-chunks/chunk-7-30-40-46-5-1100-1.wav"
import sys, wave
with wave.open(sys.stdout.buffer, 'wb') as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(8000)
    output.writeframes(b'\x01\x00')
PY
audio_identity='runtime=0123456789abcdef&station_id=gate_main&generation=7'
audio_v1() {
  PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
    DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-v1-chunks" \
    TEST_AUDIO_CHANGE_FILE="$workspace/audio-change" \
    TEST_AUDIO_TEMP_PATH="$workspace/audio-temporary" \
    TEST_AUDIO_BYTES="${TEST_AUDIO_BYTES:-46}" REQUEST_METHOD="${TEST_AUDIO_METHOD:-GET}" \
    PATH_INFO=/api/v1/audio/latest.wav QUERY_STRING="$1" \
    sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-v1"
}
audio_has() {
  if ! grep -aFq "$1" "$workspace/audio-v1"; then
    printf 'FAIL audio CGI: expected %s\n' "$1" >&2
    sed -n '1,/^\r$/p' "$workspace/audio-v1" >&2
    exit 1
  fi
}
audio_lacks() {
  if grep -aFq "$1" "$workspace/audio-v1"; then
    printf 'FAIL audio CGI: unexpected %s\n' "$1" >&2
    exit 1
  fi
}
audio_error() {
  audio_has "Status: $1"
  audio_has "\"code\":\"$2\""
  audio_has '"error":"'
  audio_lacks 'Content-Type: audio/wav'
}
audio_headers() {
  audio_has 'X-Doorfast-Runtime-Id: 0123456789abcdef'
  audio_has 'X-Doorfast-Station-Id: gate_main'
  audio_has 'X-Doorfast-Generation: 7'
}
audio_v1 "$audio_identity&after=30"
audio_has 'Content-Type: audio/wav'
audio_headers
audio_has 'Content-Length: 46'
audio_has 'ETag: "df-audio-7-40"'
audio_has 'X-Doorfast-Audio-Previous-Revision: 30'
audio_has 'X-Doorfast-Audio-Revision: 40'
audio_has 'X-Doorfast-Audio-Dropped-Bytes: 5'
audio_has 'X-Doorfast-Audio-Discontinuity: 1'
audio_has 'X-Doorfast-Audio-Timestamp-Ms: 1100'
audio_has 'X-Doorfast-Audio-Latest-Timestamp-Ms: 1200'
audio_has 'X-Doorfast-Audio-Format: s16le'
audio_has 'X-Doorfast-Audio-Sample-Rate: 8000'
audio_has 'X-Doorfast-Audio-Channels: 1'
tail -c 46 "$workspace/audio-v1" | cmp - "$workspace/audio-v1-chunks/chunk-7-30-40-46-5-1100-1.wav"
if [ -e "$(sed -n '1p' "$workspace/audio-temporary")" ]; then
  printf '%s\n' 'FAIL audio CGI: copied temporary WAV was not removed' >&2
  exit 1
fi
/bin/cp "$workspace/audio-v1-chunks/chunk-7-30-40-46-5-1100-1.wav" \
  "$workspace/audio-v1-chunks/chunk-7-20-30-46-0-1000-0.wav"
(HTTP_IF_NONE_MATCH='"df-audio-7-40"' audio_v1 "$audio_identity&after=20")
audio_has 'Content-Type: audio/wav'
audio_has 'ETag: "df-audio-7-30"'
audio_has 'X-Doorfast-Audio-Previous-Revision: 20'
audio_has 'X-Doorfast-Audio-Revision: 30'
audio_has 'X-Doorfast-Audio-Timestamp-Ms: 1000'
audio_has 'X-Doorfast-Audio-Latest-Timestamp-Ms: 1200'
audio_has 'X-Doorfast-Audio-Discontinuity: 0'
for station in a aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa; do
  (TEST_AUDIO_STATION="$station" audio_v1 "runtime=0123456789abcdef&station_id=$station&generation=7")
  audio_has 'Content-Type: audio/wav'
  audio_has "X-Doorfast-Station-Id: $station"
done
for query in \
  'runtime=0123456789abcdef&generation=7' \
  'station_id=gate_main&generation=7' \
  'runtime=0123456789abcdef&station_id=gate_main' \
  'runtime=0123456789abcdef&station_id&generation=7' \
  "$audio_identity&runtime=0123456789abcdef" \
  "$audio_identity&station_id=gate_main" "$audio_identity&generation=7" \
  "$audio_identity&after=30&after=30" "$audio_identity&after=" \
  'runtime=&station_id=gate_main&generation=7' \
  'runtime=0123456789abcdeF&station_id=gate_main&generation=7' \
  'runtime=0123456789abcdef0&station_id=gate_main&generation=7' \
  'runtime=0123456789abcdef&station_id=&generation=7' \
  'runtime=0123456789abcdef&station_id=gate-main&generation=7' \
  'runtime=0123456789abcdef&station_id=1gate&generation=7' \
  'runtime=0123456789abcdef&station_id=Gate&generation=7' \
  'runtime=0123456789abcdef&station_id=門&generation=7' \
  'runtime=0123456789abcdef&station_id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&generation=7' \
  'runtime=0123456789abcdef&station_id=gate_main%0d%0aInjected:yes&generation=7' \
  'runtime=0123456789abcdef&station_id=gate_main&generation=' \
  'runtime=0123456789abcdef&station_id=gate_main&generation=0' \
  'runtime=0123456789abcdef&station_id=gate_main&generation=07' \
  'runtime=0123456789abcdef&station_id=gate_main&generation=18446744073709551616' \
  'runtime=0123456789abcdef&station_id=gate_main&generation=184467440737095516150' \
  "$audio_identity&after=18446744073709551616" \
  "$audio_identity&after=184467440737095516150" "$audio_identity&after=01" \
  "$audio_identity&after=../30" "$audio_identity&after=30%0d%0a"; do
  audio_v1 "$query"
  audio_error '400 Bad Request' invalid_request
  audio_lacks 'X-Doorfast-Runtime-Id:'
done
(TEST_AUDIO_METHOD=POST audio_v1 "$audio_identity")
audio_error '400 Bad Request' invalid_request
for query in "$audio_identity&after=40" "$audio_identity"; do
  (HTTP_IF_NONE_MATCH='"df-audio-7-40"' audio_v1 "$query")
  audio_has 'Status: 304 Not Modified'
  audio_headers
  audio_lacks 'Content-Type: audio/wav'
done
for change in 'TEST_AUDIO_RUNTIME=fedcba9876543210' \
  'TEST_AUDIO_STATION=gate_side' 'TEST_AUDIO_SESSION=ended' \
  'TEST_AUDIO_CALL_GENERATION=8'; do
  printf '%s\n' "$change" >"$workspace/audio-change"
  (TEST_AUDIO_READY=false HTTP_IF_NONE_MATCH='"df-audio-7-40"' audio_v1 "$audio_identity&after=40")
  audio_error '409 Conflict' audio_identity_mismatch
  audio_lacks 'X-Doorfast-Runtime-Id:'
  rm "$workspace/audio-change"
  (TEST_AUDIO_COPY_CHANGE="$change" audio_v1 "$audio_identity")
  audio_error '409 Conflict' audio_identity_mismatch
  audio_lacks 'X-Doorfast-Runtime-Id:'
  rm "$workspace/audio-change"
done
(TEST_AUDIO_READY=false audio_v1 "$audio_identity")
audio_error '404 Not Found' audio_unavailable
audio_headers
(TEST_UBUS_FAIL_METHOD=status audio_v1 "$audio_identity")
audio_error '503 Service Unavailable' audio_temporarily_unavailable
audio_lacks 'X-Doorfast-Runtime-Id:'
audio_v1 "$audio_identity&after=10"
audio_error '409 Conflict' audio_cursor_expired
(TEST_AUDIO_COPY_FAIL=1 audio_v1 "$audio_identity")
audio_error '503 Service Unavailable' audio_temporarily_unavailable
(TEST_AUDIO_TEMP_FAIL=1 audio_v1 "$audio_identity")
audio_error '503 Service Unavailable' audio_temporarily_unavailable
(TEST_AUDIO_TIMESTAMP=1099 audio_v1 "$audio_identity")
audio_error '503 Service Unavailable' audio_temporarily_unavailable
for change in 'TEST_AUDIO_READY=false' 'TEST_AUDIO_TIMESTAMP=1099'; do
  (TEST_AUDIO_COPY_CHANGE="$change" audio_v1 "$audio_identity")
  audio_error '503 Service Unavailable' audio_temporarily_unavailable
  rm "$workspace/audio-change"
done

# Legacy readers must accept new eight-field files without requiring identity.
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-v1-chunks" \
  TEST_AUDIO_BYTES=46 PATH_INFO=/api/v1/audio/latest.wav \
  QUERY_STRING='generation=7&after=30' \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-v1"
audio_has 'Content-Type: audio/wav'
audio_has 'X-Doorfast-Audio-Revision: 40'
audio_lacks '"code":'

# Existing six-field files have no publication metadata for identity readers.
DOORFAST_AUDIO_CHUNK_DIRECTORY="$workspace/audio-chunks" \
  PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  PATH_INFO=/api/v1/audio/latest.wav REQUEST_METHOD=GET QUERY_STRING="$audio_identity" \
  sh package/doorfast/files/doorfast-http.sh >"$workspace/audio-v1"
audio_error '503 Service Unavailable' audio_temporarily_unavailable

# Exact uint64 arithmetic: latest can differ by one above signed/float precision.
/bin/cp "$workspace/audio-v1-chunks/chunk-7-30-40-46-5-1100-1.wav" \
  "$workspace/audio-v1-chunks/chunk-18446744073709551615-0-18446744073709551615-46-18446744073709551615-18446744073709551614-0.wav"
(TEST_AUDIO_CALL_GENERATION=18446744073709551615 \
  TEST_AUDIO_GENERATION=18446744073709551615 TEST_AUDIO_REVISION=18446744073709551615 \
  TEST_AUDIO_PREVIOUS_REVISION=0 TEST_AUDIO_TIMESTAMP=18446744073709551615 \
  audio_v1 'runtime=0123456789abcdef&station_id=gate_main&generation=18446744073709551615&after=0')
audio_has 'Content-Type: audio/wav'
audio_has 'X-Doorfast-Audio-Revision: 18446744073709551615'
audio_has 'X-Doorfast-Audio-Dropped-Bytes: 18446744073709551615'
audio_has 'X-Doorfast-Audio-Timestamp-Ms: 18446744073709551614'
audio_has 'X-Doorfast-Audio-Latest-Timestamp-Ms: 18446744073709551615'
(TEST_AUDIO_CALL_GENERATION=18446744073709551615 \
  TEST_AUDIO_GENERATION=18446744073709551615 TEST_AUDIO_REVISION=18446744073709551615 \
  TEST_AUDIO_PREVIOUS_REVISION=0 TEST_AUDIO_TIMESTAMP=18446744073709551613 \
  audio_v1 'runtime=0123456789abcdef&station_id=gate_main&generation=18446744073709551615&after=0')
audio_error '503 Service Unavailable' audio_temporarily_unavailable
(TEST_AUDIO_REVISION=18446744073709551615 audio_v1 "$audio_identity&after=18446744073709551615")
audio_has 'Status: 304 Not Modified'
audio_headers

# Filename metadata cannot replace measuring the actual copied WAV size.
rm "$workspace/audio-v1-chunks/chunk-7-30-40-46-5-1100-1.wav"
for spec in '3244 3244 valid' '3246 3246 invalid' '46 3246 invalid' \
  '44 43 invalid' '45 45 invalid' '46 46 invalidflag' \
  '46 46 overflowdrop' '46 46 overflowtime' '46 46 overflowrevision'; do
  set -- $spec
  bytes="$1" actual="$2" kind="$3" drop=0 timestamp=1100 flag=0 revision=40
  case "$kind" in
    invalidflag) flag=2 ;;
    overflowdrop) drop=18446744073709551616 ;;
    overflowtime) timestamp=18446744073709551616 ;;
    overflowrevision) revision=18446744073709551616 ;;
  esac
  fixture="$workspace/audio-v1-chunks/chunk-7-30-$revision-$bytes-$drop-$timestamp-$flag.wav"
  dd if=/dev/zero bs=1 count="$actual" 2>/dev/null >"$fixture"
  (TEST_AUDIO_BYTES="$bytes" audio_v1 "$audio_identity")
  if [ "$kind" = valid ]; then
    audio_has 'Content-Type: audio/wav'
    audio_has 'Content-Length: 3244'
    tail -c 3244 "$workspace/audio-v1" | cmp - "$fixture"
  else
    audio_error '503 Service Unavailable' audio_temporarily_unavailable
  fi
  rm "$fixture"
done
printf '%s\n' 'PASS: legacy/control/video and v1 audio identity, cursor, race, metadata and bounds'

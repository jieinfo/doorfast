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
if [ -n "${TEST_UBUS_ERROR_CODE:-}" ] && [ "$method" = monitor_start ]; then
  if [ "${TEST_UBUS_ERROR_CODE}" = capacity_busy ]; then
    printf '%s\n' '{"error":{"code":"capacity_busy","configured_capacity":2,"effective_capacity":1,"active_encoders":1}}'
  else
    printf '{"error":{"code":"%s"}}\n' "${TEST_UBUS_ERROR_CODE}"
  fi
  exit 0
fi
if [ "$*" = 'call doorfast status' ]; then
  printf '{"call":{"generation":%s},"video":{"ready":%s,"generation":%s,"frame_no":%s,"bytes":%s},"audio":{"snapshot_ready":%s,"generation":%s,"snapshot_packet_count":%s,"snapshot_previous_packet_count":%s,"snapshot_bytes":%s,"snapshot_dropped_bytes":%s}}\n' \
    "${TEST_CALL_GENERATION:-7}" "${TEST_VIDEO_READY:-true}" \
    "${TEST_VIDEO_GENERATION:-7}" "${TEST_VIDEO_FRAME:-12}" \
    "${TEST_VIDEO_BYTES:-4}" "${TEST_AUDIO_READY:-true}" \
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
  '@.call.generation') printf '%s\n' "${TEST_CALL_GENERATION:-7}" ;;
  '@.video.ready') printf '%s\n' "${TEST_VIDEO_READY:-true}" ;;
  '@.video.generation') printf '%s\n' "${TEST_VIDEO_GENERATION:-7}" ;;
  '@.video.frame_no') printf '%s\n' "${TEST_VIDEO_FRAME:-12}" ;;
  '@.video.bytes') printf '%s\n' "${TEST_VIDEO_BYTES:-4}" ;;
  '@.audio.snapshot_ready') printf '%s\n' "${TEST_AUDIO_READY:-true}" ;;
  '@.audio.generation') printf '%s\n' "${TEST_AUDIO_GENERATION:-7}" ;;
  '@.audio.snapshot_packet_count') printf '%s\n' "${TEST_AUDIO_REVISION:-40}" ;;
  '@.audio.snapshot_previous_packet_count') printf '%s\n' "${TEST_AUDIO_PREVIOUS_REVISION:-30}" ;;
  '@.audio.snapshot_bytes') printf '%s\n' "${TEST_AUDIO_BYTES:-12}" ;;
  '@.audio.snapshot_dropped_bytes') printf '%s\n' "${TEST_AUDIO_DROPPED_BYTES:-0}" ;;
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
run /api/v1/unlock '{"runtime_id":"0123456789abcdef","generation":7}'
run /api/v1/answer '{"runtime_id":"0123456789abcdef","generation":7,"primary_media_port":8303}'
run /api/v1/hangup '{"runtime_id":"0123456789abcdef","generation":7,"reason":"ha"}'
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
test "$(wc -l <"$trace" | tr -d ' ')" -eq 12
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
grep -Fxq 'call doorfast status' "$trace"
grep -Fxq 'call doorfast unlock {"runtime_id":"0123456789abcdef","generation":7}' "$trace"
grep -Fxq 'call doorfast answer {"runtime_id":"0123456789abcdef","generation":7,"primary_media_port":8303}' "$trace"
grep -Fxq 'call doorfast hangup {"runtime_id":"0123456789abcdef","generation":7,"reason":"ha"}' "$trace"
grep -Fxq 'call doorfast call_elevator {"runtime_id":"0123456789abcdef","direction":"up"}' "$trace"
trace_lines="$(wc -l <"$trace" | tr -d ' ')"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  PATH_INFO=/api/v1/unknown sh package/doorfast/files/doorfast-http.sh \
  >"$workspace/unknown"
grep -aFq 'Status: 404 Not Found' "$workspace/unknown"
test "$(wc -l <"$trace" | tr -d ' ')" -eq "$trace_lines"

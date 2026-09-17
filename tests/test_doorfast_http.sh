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
if [ "$*" = 'call doorfast status' ]; then
  printf '{"call":{"generation":%s},"video":{"ready":%s,"generation":%s,"frame_no":%s,"bytes":%s},"audio":{"snapshot_ready":%s,"generation":%s,"snapshot_packet_count":%s,"snapshot_previous_packet_count":%s,"snapshot_bytes":%s,"snapshot_dropped_bytes":%s}}\n' \
    "${TEST_CALL_GENERATION:-7}" "${TEST_VIDEO_READY:-true}" \
    "${TEST_VIDEO_GENERATION:-7}" "${TEST_VIDEO_FRAME:-12}" \
    "${TEST_VIDEO_BYTES:-4}" "${TEST_AUDIO_READY:-true}" \
    "${TEST_AUDIO_GENERATION:-7}" "${TEST_AUDIO_REVISION:-40}" \
    "${TEST_AUDIO_PREVIOUS_REVISION:-30}" "${TEST_AUDIO_BYTES:-12}" \
    "${TEST_AUDIO_DROPPED_BYTES:-0}"
else
  printf '{"ok":true}\n'
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
run /api/v1/unlock '{"generation":7}'
run /api/v1/answer '{"generation":7,"primary_media_port":8303}'
run /api/v1/hangup '{"generation":7,"reason":"ha"}'
run /api/v1/call_elevator '{"direction":"up"}'
run_method /api/v1/monitor/start '' POST >"$workspace/monitor-start"
run_method /api/v1/monitor/stop \
  '{"generation":7}' POST >"$workspace/monitor-stop"
run_method /api/v1/monitor/viewer \
  '{"active":true,"generation":7}' POST \
  >"$workspace/monitor-viewer"
run_method /api/v1/monitor/status '' GET >"$workspace/monitor-status"
test "$(wc -l <"$trace" | tr -d ' ')" -eq 9
grep -Fxq 'call doorfast monitor_start {}' "$trace"
grep -Fxq 'call doorfast monitor_stop {"generation":7}' "$trace"
grep -Fxq 'call doorfast monitor_viewer {"generation":7,"active":true}' "$trace"
grep -Fxq 'call doorfast monitor_status {}' "$trace"
! grep -Fq 'secret-' "$trace"
trace_lines="$(wc -l <"$trace" | tr -d ' ')"
run_method /api/v1/monitor/stop \
  '{"generation":7,"ignored":"secret-stop"}' POST \
  >"$workspace/monitor-extra"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-extra"
run_method /api/v1/monitor/viewer \
  '{"generation":7,"active":true,"ignored":"secret-viewer"}' POST \
  >"$workspace/monitor-viewer-extra"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-viewer-extra"
run_method /api/v1/monitor/stop '{"generation":0}' POST \
  >"$workspace/monitor-invalid"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-invalid"
run_method /api/v1/monitor/start '' GET >"$workspace/monitor-wrong-method"
grep -aFq 'Status: 405 Method Not Allowed' "$workspace/monitor-wrong-method"
run_method /api/v1/monitor/viewer \
  '{"generation":7,"active":1}' POST >"$workspace/monitor-invalid-active"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-invalid-active"
run_method /api/v1/monitor/viewer \
  '{"generation":7}' POST >"$workspace/monitor-missing-active"
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-missing-active"
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
! grep -aFq 'Status: 503 Service Unavailable' "$workspace/monitor-empty-success"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_stop TEST_UBUS_FAIL_CODE=2 \
  PATH_INFO=/api/v1/monitor/stop REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=16; \
  printf '%s' '{"generation":7}' | sh package/doorfast/files/doorfast-http.sh \
    >"$workspace/monitor-invalid-ubus")
grep -aFq 'Status: 400 Bad Request' "$workspace/monitor-invalid-ubus"
grep -aFq '"error":"invalid monitor request"' \
  "$workspace/monitor-invalid-ubus"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_stop TEST_UBUS_FAIL_CODE=4 \
  PATH_INFO=/api/v1/monitor/stop REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=16; \
  printf '%s' '{"generation":7}' | sh package/doorfast/files/doorfast-http.sh \
    >"$workspace/monitor-generation-conflict")
grep -aFq 'Status: 409 Conflict' "$workspace/monitor-generation-conflict"
grep -aFq '"error":"monitor generation mismatch"' \
  "$workspace/monitor-generation-conflict"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_start TEST_UBUS_FAIL_CODE=8 \
  PATH_INFO=/api/v1/monitor/start REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=0; \
  sh package/doorfast/files/doorfast-http.sh </dev/null \
    >"$workspace/monitor-state-conflict")
grep -aFq 'Status: 409 Conflict' "$workspace/monitor-state-conflict"
grep -aFq '"error":"monitor state does not allow operation"' \
  "$workspace/monitor-state-conflict"
(export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  TEST_UBUS_FAIL_METHOD=monitor_stop TEST_UBUS_FAIL_CODE=1 \
  PATH_INFO=/api/v1/monitor/stop REQUEST_METHOD=POST \
  CONTENT_TYPE=application/json CONTENT_LENGTH=16; \
  printf '%s' '{"generation":7}' | sh package/doorfast/files/doorfast-http.sh \
    >"$workspace/monitor-unavailable")
grep -aFq 'Status: 503 Service Unavailable' "$workspace/monitor-unavailable"
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
grep -Fxq 'call doorfast unlock {"generation":7}' "$trace"
grep -Fxq 'call doorfast answer {"generation":7,"primary_media_port":8303}' "$trace"
grep -Fxq 'call doorfast hangup {"generation":7,"reason":"ha"}' "$trace"
grep -Fxq 'call doorfast call_elevator {"direction":"up"}' "$trace"
trace_lines="$(wc -l <"$trace" | tr -d ' ')"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" \
  PATH_INFO=/api/v1/unknown sh package/doorfast/files/doorfast-http.sh \
  >"$workspace/unknown"
grep -aFq 'Status: 404 Not Found' "$workspace/unknown"
test "$(wc -l <"$trace" | tr -d ' ')" -eq "$trace_lines"

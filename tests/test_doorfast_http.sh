#!/bin/sh
set -eu
workspace=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-http.XXXXXXXX")
trap 'rm -rf "$workspace"' EXIT
fakebin="$workspace/bin"; trace="$workspace/trace"; mkdir -p "$fakebin"
cat >"$fakebin/ubus" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$DOORFAST_HTTP_TRACE"
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
  if [ "$1" = -e ]; then expression="$2"; shift 2; else shift; fi
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
  *) exit 1 ;;
esac
EOF
chmod +x "$fakebin/ubus" "$fakebin/jsonfilter"
run() {
  (export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" PATH_INFO="$1" CONTENT_LENGTH="${#2}"; printf '%s' "$2" | sh package/doorfast/files/doorfast-http.sh >/dev/null)
}
run /api/v1/status ''
run /api/v1/unlock '{"generation":7}'
run /api/v1/answer '{"generation":7,"primary_media_port":8303}'
run /api/v1/hangup '{"generation":7,"reason":"ha"}'
run /api/v1/call_elevator '{"direction":"up"}'
test "$(wc -l <"$trace" | tr -d ' ')" -eq 5
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
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" PATH_INFO=/api/v1/unknown sh package/doorfast/files/doorfast-http.sh >/dev/null
test "$(wc -l <"$trace" | tr -d ' ')" -eq "$trace_lines"

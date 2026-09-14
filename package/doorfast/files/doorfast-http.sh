#!/bin/sh
path="${PATH_INFO:-}"
[ -n "$path" ] || path="${QUERY_STRING#path=}"

json_field() {
  jsonfilter -s "$1" -e "$2" 2>/dev/null
}

query_generation() {
  local item key value old_ifs
  REQUESTED_GENERATION=''
  old_ifs="$IFS"
  IFS='&'
  for item in ${QUERY_STRING:-}; do
    key="${item%%=*}"
    value="${item#*=}"
    if [ "$key" = generation ]; then
      REQUESTED_GENERATION="$value"
      break
    fi
  done
  IFS="$old_ifs"
}

read_video_status() {
  local status
  status="$(ubus call doorfast status 2>/dev/null)" || return 1
  VIDEO_READY="$(json_field "$status" '@.video.ready')"
  VIDEO_GENERATION="$(json_field "$status" '@.video.generation')"
  VIDEO_FRAME_NO="$(json_field "$status" '@.video.frame_no')"
  VIDEO_BYTES="$(json_field "$status" '@.video.bytes')"
  CALL_GENERATION="$(json_field "$status" '@.call.generation')"
  case "$VIDEO_READY" in true|1) ;; *) return 1 ;; esac
  case "$VIDEO_GENERATION:$VIDEO_FRAME_NO:$VIDEO_BYTES:$CALL_GENERATION" in
    *[!0-9:]*|:*|*::*|*:) return 1 ;;
  esac
  [ "$VIDEO_GENERATION" = "$CALL_GENERATION" ]
}

read_audio_status() {
  local status
  status="$(ubus call doorfast status 2>/dev/null)" || return 1
  AUDIO_READY="$(json_field "$status" '@.audio.snapshot_ready')"
  AUDIO_GENERATION="$(json_field "$status" '@.audio.generation')"
  AUDIO_REVISION="$(json_field "$status" '@.audio.snapshot_packet_count')"
  AUDIO_BYTES="$(json_field "$status" '@.audio.snapshot_bytes')"
  CALL_GENERATION="$(json_field "$status" '@.call.generation')"
  case "$AUDIO_READY" in true|1) ;; *) return 1 ;; esac
  case "$AUDIO_GENERATION:$AUDIO_REVISION:$AUDIO_BYTES:$CALL_GENERATION" in
    *[!0-9:]*|:*|*::*|*:) return 1 ;;
  esac
  [ "$AUDIO_GENERATION" = "$CALL_GENERATION" ]
}

if [ "$path" = /api/v1/video/latest.jpg ]; then
  snapshot="${DOORFAST_VIDEO_SNAPSHOT:-/tmp/doorfast-latest.jpg}"
  query_generation
  case "$REQUESTED_GENERATION" in
    ''|*[!0-9]*)
      if [ -n "$REQUESTED_GENERATION" ]; then
        printf 'Status: 400 Bad Request\r\nContent-Type: application/json\r\n\r\n'
        printf '{"error":"invalid video generation"}\n'
        exit 0
      fi
      ;;
  esac
  if ! read_video_status || [ ! -r "$snapshot" ]; then
    printf 'Status: 404 Not Found\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"video frame unavailable"}\n'
    exit 0
  fi
  if [ -n "$REQUESTED_GENERATION" ] &&
     [ "$REQUESTED_GENERATION" != "$VIDEO_GENERATION" ]; then
    printf 'Status: 409 Conflict\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"video generation mismatch","generation":%s}\n' \
      "$VIDEO_GENERATION"
    exit 0
  fi
  etag="\"df-$VIDEO_GENERATION-$VIDEO_FRAME_NO\""
  if [ "${HTTP_IF_NONE_MATCH:-}" = "$etag" ]; then
    printf 'Status: 304 Not Modified\r\nETag: %s\r\nCache-Control: no-cache\r\n\r\n' "$etag"
    exit 0
  fi
  temporary="$(mktemp "${TMPDIR:-/tmp}/doorfast-video.XXXXXX")" || {
    printf 'Status: 503 Service Unavailable\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"video frame temporarily unavailable"}\n'
    exit 0
  }
  trap 'rm -f "$temporary"' EXIT HUP INT TERM
  expected_generation="$VIDEO_GENERATION"
  expected_frame_no="$VIDEO_FRAME_NO"
  expected_bytes="$VIDEO_BYTES"
  if ! cp "$snapshot" "$temporary" ||
     ! read_video_status ||
     [ "$VIDEO_GENERATION" != "$expected_generation" ] ||
     [ "$VIDEO_FRAME_NO" != "$expected_frame_no" ] ||
     [ "$VIDEO_BYTES" != "$expected_bytes" ] ||
     [ "$(wc -c <"$temporary" | tr -d ' ')" != "$expected_bytes" ]; then
    printf 'Status: 503 Service Unavailable\r\nContent-Type: application/json\r\nRetry-After: 1\r\n\r\n'
    printf '{"error":"video frame changed during read"}\n'
    exit 0
  fi
  printf 'Content-Type: image/jpeg\r\nContent-Length: %s\r\nCache-Control: no-cache\r\nETag: %s\r\nX-Doorfast-Generation: %s\r\nX-Doorfast-Frame: %s\r\n\r\n' \
    "$expected_bytes" "$etag" "$expected_generation" "$expected_frame_no"
  cat "$temporary"
  exit 0
fi
if [ "$path" = /api/v1/audio/latest.wav ]; then
  audio="${DOORFAST_AUDIO_SNAPSHOT:-/tmp/doorfast-latest.wav}"
  query_generation
  case "$REQUESTED_GENERATION" in
    ''|*[!0-9]*)
      if [ -n "$REQUESTED_GENERATION" ]; then
        printf 'Status: 400 Bad Request\r\nContent-Type: application/json\r\n\r\n'
        printf '{"error":"invalid audio generation"}\n'
        exit 0
      fi
      ;;
  esac
  if ! read_audio_status || [ ! -r "$audio" ]; then
    printf 'Status: 404 Not Found\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"audio unavailable"}\n'
    exit 0
  fi
  if [ -n "$REQUESTED_GENERATION" ] &&
     [ "$REQUESTED_GENERATION" != "$AUDIO_GENERATION" ]; then
    printf 'Status: 409 Conflict\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"audio generation mismatch","generation":%s}\n' \
      "$AUDIO_GENERATION"
    exit 0
  fi
  etag="\"df-audio-$AUDIO_GENERATION-$AUDIO_REVISION\""
  if [ "${HTTP_IF_NONE_MATCH:-}" = "$etag" ]; then
    printf 'Status: 304 Not Modified\r\nETag: %s\r\nCache-Control: no-cache\r\n\r\n' "$etag"
    exit 0
  fi
  temporary="$(mktemp "${TMPDIR:-/tmp}/doorfast-audio.XXXXXX")" || {
    printf 'Status: 503 Service Unavailable\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"audio temporarily unavailable"}\n'
    exit 0
  }
  trap 'rm -f "$temporary"' EXIT HUP INT TERM
  expected_generation="$AUDIO_GENERATION"
  expected_revision="$AUDIO_REVISION"
  expected_bytes="$AUDIO_BYTES"
  if ! cp "$audio" "$temporary" ||
     ! read_audio_status ||
     [ "$AUDIO_GENERATION" != "$expected_generation" ] ||
     [ "$AUDIO_REVISION" != "$expected_revision" ] ||
     [ "$AUDIO_BYTES" != "$expected_bytes" ] ||
     [ "$(wc -c <"$temporary" | tr -d ' ')" != "$expected_bytes" ]; then
    printf 'Status: 503 Service Unavailable\r\nContent-Type: application/json\r\nRetry-After: 1\r\n\r\n'
    printf '{"error":"audio changed during read"}\n'
    exit 0
  fi
  printf 'Content-Type: audio/wav\r\nContent-Length: %s\r\nCache-Control: no-cache\r\nETag: %s\r\nX-Doorfast-Generation: %s\r\nX-Doorfast-Audio-Revision: %s\r\n\r\n' \
    "$expected_bytes" "$etag" "$expected_generation" "$expected_revision"
  cat "$temporary"
  exit 0
fi
printf 'Content-Type: application/json\r\n\r\n'
body=''
if [ "${CONTENT_LENGTH:-0}" -gt 0 ] 2>/dev/null; then
  body="$(dd bs=1 count="$CONTENT_LENGTH" 2>/dev/null)"
fi
case "$path" in
  /api/v1/status) ubus call doorfast status ;;
  /api/v1/unlock) [ -n "$body" ] || body='{}'; ubus call doorfast unlock "$body" ;;
  /api/v1/answer) [ -n "$body" ] || body='{}'; ubus call doorfast answer "$body" ;;
  /api/v1/hangup) [ -n "$body" ] || body='{}'; ubus call doorfast hangup "$body" ;;
  /api/v1/call_elevator) [ -n "$body" ] || body='{}'; ubus call doorfast call_elevator "$body" ;;
  *) printf '{"error":"unknown endpoint"}\n' ;;
esac

#!/bin/sh
path="${PATH_INFO:-}"
[ -n "$path" ] || path="${QUERY_STRING#path=}"
if [ "$path" = /api/v1/video/latest.jpg ]; then
  snapshot="${DOORFAST_VIDEO_SNAPSHOT:-/tmp/doorfast-latest.jpg}"
  if [ -r "$snapshot" ]; then
    printf 'Content-Type: image/jpeg\r\nCache-Control: no-store\r\n\r\n'
    cat "$snapshot"
  else
    printf 'Status: 404 Not Found\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"video frame unavailable"}\n'
  fi
  exit 0
fi
if [ "$path" = /api/v1/audio/latest.wav ]; then
  audio="${DOORFAST_AUDIO_SNAPSHOT:-/tmp/doorfast-latest.wav}"
  if [ -r "$audio" ]; then
    printf 'Content-Type: audio/wav\r\nCache-Control: no-store\r\n\r\n'
    cat "$audio"
  else
    printf 'Status: 404 Not Found\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"audio unavailable"}\n'
  fi
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

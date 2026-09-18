#!/bin/sh
helper=/usr/sbin/doorfast-pcm-http
path="${PATH_INFO:-}"
[ -n "$path" ] || path="${QUERY_STRING#path=}"

case "$path" in
  /api/v1/audio/session|/api/v1/audio/submit.pcm|/api/v1/audio/session/end)
    exec "$helper"
    ;;
esac

json_field() {
  jsonfilter -s "$1" -e "$2" 2>/dev/null
}

http_error() {
  local status="$1" message="$2" allow="${3:-}"
  printf 'Status: %s\r\n' "$status"
  [ -z "$allow" ] || printf 'Allow: %s\r\n' "$allow"
  printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'
  printf '{"error":"%s"}\n' "$message"
}

uint64_nonzero() {
  local value="$1" maximum=18446744073709551615 left right left_digit right_digit
  case "$value" in ''|0|0*|*[!0-9]*) return 1 ;; esac
  [ "${#value}" -le 20 ] || return 1
  [ "${#value}" -lt 20 ] && return 0
  left="$value"
  right="$maximum"
  while [ -n "$left" ]; do
    left_digit="${left%"${left#?}"}"
    right_digit="${right%"${right#?}"}"
    [ "$left_digit" -lt "$right_digit" ] && return 0
    [ "$left_digit" -gt "$right_digit" ] && return 1
    left="${left#?}"
    right="${right#?}"
  done
  return 0
}

monitor_body() {
  local length="${CONTENT_LENGTH:-0}"
  case "$length" in ''|*[!0-9]*) return 1 ;; esac
  [ "$length" -le 4096 ] || return 1
  MONITOR_BODY=''
  if [ "$length" -gt 0 ]; then
    MONITOR_BODY="$(dd bs=1 count="$length" 2>/dev/null)" || return 1
    [ "${#MONITOR_BODY}" -eq "$length" ] || return 1
  fi
  MONITOR_COMPACT="$(printf '%s' "$MONITOR_BODY" | tr -d ' \t\r\n')"
  return 0
}

monitor_json_content_type() {
  case "${CONTENT_TYPE:-}" in
    application/json|application/json';'*) return 0 ;;
    *) return 1 ;;
  esac
}

monitor_call() {
  local method="$1" arguments="$2" output status
  if [ -n "$arguments" ]; then
    if output="$(ubus call doorfast "$method" "$arguments" 2>/dev/null)"; then
      status=0
    else
      status=$?
    fi
  else
    if output="$(ubus call doorfast "$method" 2>/dev/null)"; then
      status=0
    else
      status=$?
    fi
  fi
  if [ "$status" -ne 0 ]; then
    case "$status" in
      2) http_error '400 Bad Request' 'invalid monitor request' ;;
      4) http_error '409 Conflict' 'monitor generation mismatch' ;;
      8) http_error '409 Conflict' 'monitor state does not allow operation' ;;
      *) http_error '503 Service Unavailable' 'monitor service unavailable' ;;
    esac
    return
  fi
  printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'
  printf '%s\n' "$output"
}

query_generation() {
  local item key value old_ifs
  REQUESTED_GENERATION=''
  REQUESTED_AFTER=''
  old_ifs="$IFS"
  IFS='&'
  for item in ${QUERY_STRING:-}; do
    key="${item%%=*}"
    value="${item#*=}"
    if [ "$key" = generation ]; then
      REQUESTED_GENERATION="$value"
    elif [ "$key" = after ]; then
      REQUESTED_AFTER="$value"
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
  AUDIO_PREVIOUS_REVISION="$(json_field "$status" '@.audio.snapshot_previous_packet_count')"
  AUDIO_BYTES="$(json_field "$status" '@.audio.snapshot_bytes')"
  AUDIO_DROPPED_BYTES="$(json_field "$status" '@.audio.snapshot_dropped_bytes')"
  CALL_GENERATION="$(json_field "$status" '@.call.generation')"
  case "$AUDIO_READY" in true|1) ;; *) return 1 ;; esac
  case "$AUDIO_GENERATION:$AUDIO_REVISION:$AUDIO_PREVIOUS_REVISION:$AUDIO_BYTES:$AUDIO_DROPPED_BYTES:$CALL_GENERATION" in
    *[!0-9:]*|:*|*::*|*:) return 1 ;;
  esac
  [ "$AUDIO_GENERATION" = "$CALL_GENERATION" ]
}

select_audio_chunk() {
  local candidate filename stem old_ifs
  for candidate in "${DOORFAST_AUDIO_CHUNK_DIRECTORY:-/tmp/doorfast-audio-chunks}"/chunk-"$AUDIO_GENERATION"-"$SELECT_AFTER"-*.wav; do
    [ -r "$candidate" ] || continue
    filename="${candidate##*/}"
    stem="${filename%.wav}"
    old_ifs="$IFS"
    IFS='-'
    set -- $stem
    IFS="$old_ifs"
    [ "$#" -eq 6 ] && [ "$1" = chunk ] || continue
    case "$2:$3:$4:$5:$6" in *[!0-9:]*|:*|*::*|*:) continue ;; esac
    [ "$2" = "$AUDIO_GENERATION" ] &&
      [ "$3" = "$SELECT_AFTER" ] || continue
    SELECTED_AUDIO="$candidate"
    SELECTED_PREVIOUS_REVISION="$3"
    SELECTED_REVISION="$4"
    SELECTED_BYTES="$5"
    SELECTED_DROPPED_BYTES="$6"
    return 0
  done
  return 1
}

if [ "$path" = /api/v1/stations ]; then
  [ "${REQUEST_METHOD:-}" = GET ] || {
    http_error '405 Method Not Allowed' 'method not allowed' GET
    exit 0
  }
  case "${CONTENT_LENGTH:-0}" in
    ''|*[!0-9]*)
      http_error '400 Bad Request' 'invalid station request'
      exit 0
      ;;
    0) ;;
    *)
      http_error '400 Bad Request' 'invalid station request'
      exit 0
      ;;
  esac
  [ -z "${QUERY_STRING:-}" ] || {
    http_error '400 Bad Request' 'invalid station request'
    exit 0
  }
  stations="$(ubus call doorfast stations 2>/dev/null)" || {
    http_error '503 Service Unavailable' 'station service unavailable'
    exit 0
  }
  printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'
  printf '%s\n' "$stations"
  exit 0
fi

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
  case "$REQUESTED_AFTER" in
    ''|*[!0-9]*)
      if [ -n "$REQUESTED_AFTER" ]; then
        printf 'Status: 400 Bad Request\r\nContent-Type: application/json\r\n\r\n'
        printf '{"error":"invalid audio cursor"}\n'
        exit 0
      fi
      ;;
  esac
  if [ -n "$REQUESTED_AFTER" ] && [ -z "$REQUESTED_GENERATION" ]; then
    printf 'Status: 400 Bad Request\r\nContent-Type: application/json\r\n\r\n'
    printf '{"error":"audio cursor requires generation"}\n'
    exit 0
  fi
  if ! read_audio_status; then
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
  if [ -n "$REQUESTED_AFTER" ] &&
     [ "$REQUESTED_AFTER" = "$AUDIO_REVISION" ]; then
    etag="\"df-audio-$AUDIO_GENERATION-$AUDIO_REVISION\""
    printf 'Status: 304 Not Modified\r\nETag: %s\r\nCache-Control: no-cache\r\n\r\n' "$etag"
    exit 0
  fi
  SELECT_AFTER="${REQUESTED_AFTER:-$AUDIO_PREVIOUS_REVISION}"
  if ! select_audio_chunk; then
    if [ -n "$REQUESTED_AFTER" ] &&
       [ "$REQUESTED_AFTER" != "$AUDIO_PREVIOUS_REVISION" ]; then
      printf 'Status: 409 Conflict\r\nContent-Type: application/json\r\n\r\n'
      printf '{"error":"audio cursor unavailable","generation":%s,"previous_revision":%s,"revision":%s}\n' \
        "$AUDIO_GENERATION" "$AUDIO_PREVIOUS_REVISION" "$AUDIO_REVISION"
    else
      printf 'Status: 503 Service Unavailable\r\nContent-Type: application/json\r\nRetry-After: 1\r\n\r\n'
      printf '{"error":"published audio chunk unavailable"}\n'
    fi
    exit 0
  fi
  etag="\"df-audio-$AUDIO_GENERATION-$SELECTED_REVISION\""
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
  expected_revision="$SELECTED_REVISION"
  expected_previous_revision="$SELECTED_PREVIOUS_REVISION"
  expected_bytes="$SELECTED_BYTES"
  expected_dropped_bytes="$SELECTED_DROPPED_BYTES"
  if ! cp "$SELECTED_AUDIO" "$temporary" ||
     ! read_audio_status ||
     [ "$AUDIO_GENERATION" != "$expected_generation" ] ||
     [ "$(wc -c <"$temporary" | tr -d ' ')" != "$expected_bytes" ]; then
    printf 'Status: 503 Service Unavailable\r\nContent-Type: application/json\r\nRetry-After: 1\r\n\r\n'
    printf '{"error":"audio changed during read"}\n'
    exit 0
  fi
  printf 'Content-Type: audio/wav\r\nContent-Length: %s\r\nCache-Control: no-cache\r\nETag: %s\r\nX-Doorfast-Generation: %s\r\nX-Doorfast-Audio-Previous-Revision: %s\r\nX-Doorfast-Audio-Revision: %s\r\nX-Doorfast-Audio-Dropped-Bytes: %s\r\n\r\n' \
    "$expected_bytes" "$etag" "$expected_generation" \
    "$expected_previous_revision" "$expected_revision" \
    "$expected_dropped_bytes"
  cat "$temporary"
  exit 0
fi

case "$path" in
  /api/v1/monitor/start)
    [ "${REQUEST_METHOD:-}" = POST ] || {
      http_error '405 Method Not Allowed' 'method not allowed' POST
      exit 0
    }
    monitor_json_content_type || {
      http_error '415 Unsupported Media Type' 'application/json required'
      exit 0
    }
    monitor_body || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    [ -z "$MONITOR_COMPACT" ] || [ "$MONITOR_COMPACT" = '{}' ] || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    monitor_call monitor_start '{}'
    exit 0
    ;;
  /api/v1/monitor/stop)
    [ "${REQUEST_METHOD:-}" = POST ] || {
      http_error '405 Method Not Allowed' 'method not allowed' POST
      exit 0
    }
    monitor_json_content_type || {
      http_error '415 Unsupported Media Type' 'application/json required'
      exit 0
    }
    monitor_body || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    generation="$(json_field "$MONITOR_BODY" '@.generation')" || generation=''
    uint64_nonzero "$generation" || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    [ "$MONITOR_COMPACT" = "{\"generation\":$generation}" ] || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    monitor_call monitor_stop "{\"generation\":$generation}"
    exit 0
    ;;
  /api/v1/monitor/viewer)
    [ "${REQUEST_METHOD:-}" = POST ] || {
      http_error '405 Method Not Allowed' 'method not allowed' POST
      exit 0
    }
    monitor_json_content_type || {
      http_error '415 Unsupported Media Type' 'application/json required'
      exit 0
    }
    monitor_body || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    generation="$(json_field "$MONITOR_BODY" '@.generation')" || generation=''
    active="$(json_field "$MONITOR_BODY" '@.active')" || active=''
    uint64_nonzero "$generation" || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    case "$active" in true|false) ;; *)
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
      ;;
    esac
    [ "$MONITOR_COMPACT" = \
        "{\"generation\":$generation,\"active\":$active}" ] ||
      [ "$MONITOR_COMPACT" = \
        "{\"active\":$active,\"generation\":$generation}" ] || {
        http_error '400 Bad Request' 'invalid monitor request'
        exit 0
      }
    monitor_call monitor_viewer \
      "{\"generation\":$generation,\"active\":$active}"
    exit 0
    ;;
  /api/v1/monitor/status)
    [ "${REQUEST_METHOD:-}" = GET ] || {
      http_error '405 Method Not Allowed' 'method not allowed' GET
      exit 0
    }
    monitor_body || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    [ -z "$MONITOR_COMPACT" ] && [ -z "${QUERY_STRING:-}" ] || {
      http_error '400 Bad Request' 'invalid monitor request'
      exit 0
    }
    monitor_call monitor_status '{}'
    exit 0
    ;;
esac

body=''
if [ "${CONTENT_LENGTH:-0}" -gt 0 ] 2>/dev/null; then
  body="$(dd bs=1 count="$CONTENT_LENGTH" 2>/dev/null)"
fi
case "$path" in
  /api/v1/status|/api/v1/unlock|/api/v1/answer|/api/v1/hangup|/api/v1/call_elevator) ;;
  *)
    http_error '404 Not Found' 'unknown endpoint'
    exit 0
    ;;
esac
printf 'Content-Type: application/json\r\n\r\n'
case "$path" in
  /api/v1/status) ubus call doorfast status ;;
  /api/v1/unlock) [ -n "$body" ] || body='{}'; ubus call doorfast unlock "$body" ;;
  /api/v1/answer) [ -n "$body" ] || body='{}'; ubus call doorfast answer "$body" ;;
  /api/v1/hangup) [ -n "$body" ] || body='{}'; ubus call doorfast hangup "$body" ;;
  /api/v1/call_elevator) [ -n "$body" ] || body='{}'; ubus call doorfast call_elevator "$body" ;;
esac

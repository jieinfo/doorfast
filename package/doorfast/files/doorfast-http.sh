#!/bin/sh
printf 'Content-Type: application/json\r\n\r\n'
path="${PATH_INFO:-}"
[ -n "$path" ] || path="${QUERY_STRING#path=}"
body=''
if [ "${CONTENT_LENGTH:-0}" -gt 0 ] 2>/dev/null; then
  body="$(dd bs=1 count="$CONTENT_LENGTH" 2>/dev/null)"
fi
case "$path" in
  /api/v1/status) ubus call doorfast status ;;
  /api/v1/unlock) ubus call doorfast unlock "${body:-{}}" ;;
  /api/v1/answer) ubus call doorfast answer "${body:-{}}" ;;
  /api/v1/hangup) ubus call doorfast hangup "${body:-{}}" ;;
  /api/v1/call_elevator) ubus call doorfast call_elevator "${body:-{}}" ;;
  *) printf '{"error":"unknown endpoint"}\n' ;;
esac

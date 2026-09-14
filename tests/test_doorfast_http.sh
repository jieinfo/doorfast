#!/bin/sh
set -eu
workspace=$(mktemp -d "${TMPDIR:-/tmp}/doorfast-http.XXXXXXXX")
trap 'rm -rf "$workspace"' EXIT
fakebin="$workspace/bin"; trace="$workspace/trace"; mkdir -p "$fakebin"
cat >"$fakebin/ubus" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$DOORFAST_HTTP_TRACE"
printf '{"ok":true}\n'
EOF
chmod +x "$fakebin/ubus"
run() {
  (export PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" PATH_INFO="$1" CONTENT_LENGTH="${#2}"; printf '%s' "$2" | sh package/doorfast/files/doorfast-http.sh >/dev/null)
}
run /api/v1/status ''
run /api/v1/unlock '{"generation":7}'
run /api/v1/answer '{"generation":7,"primary_media_port":8303}'
run /api/v1/hangup '{"generation":7,"reason":"ha"}'
run /api/v1/call_elevator '{"direction":"up"}'
test "$(wc -l <"$trace" | tr -d ' ')" -eq 5
grep -Fxq 'doorfast status' "$trace"
grep -Fxq 'doorfast unlock {"generation":7}' "$trace"
grep -Fxq 'doorfast answer {"generation":7,"primary_media_port":8303}' "$trace"
grep -Fxq 'doorfast hangup {"generation":7,"reason":"ha"}' "$trace"
grep -Fxq 'doorfast call_elevator {"direction":"up"}' "$trace"
PATH="$fakebin:$PATH" DOORFAST_HTTP_TRACE="$trace" PATH_INFO=/api/v1/unknown sh package/doorfast/files/doorfast-http.sh >/dev/null
test "$(wc -l <"$trace" | tr -d ' ')" -eq 5

#!/bin/sh
set -eu

command_text=${1-}
case "$command_text" in
    *'/etc/init.d/doorfast status'*)
        exit 0
        ;;
    *'ip -details -json link show'*)
        count=0
        if [ -f "$DOORFAST_FAKE_COUNTER" ]; then
            count=$(sed -n '1p' "$DOORFAST_FAKE_COUNTER")
        fi
        count=$((count + 1))
        printf '%s\n' "$count" > "$DOORFAST_FAKE_COUNTER"
        timer=$((100 - count))
        printf '[{"ifname":"br-lan","linkinfo":{"info_data":{"gc_timer":%s,"stp_state":0}}}]\n' "$timer"
        ;;
    *'ip -json address show'*|*'ip -json route show table all'*)
        printf '[]\n'
        ;;
    *'uci export network'*)
        printf "package 'network'\n"
        ;;
    *'nft -j list ruleset'*)
        printf '{"nftables":[]}\n'
        ;;
    *'/usr/sbin/doorfast --preflight'*)
        printf '{"safe":false,"failures":["missing_interface"]}\n'
        exit 2
        ;;
    *)
        printf 'unexpected fake SSH command: %s\n' "$command_text" >&2
        exit 64
        ;;
esac

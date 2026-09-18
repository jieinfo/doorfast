#!/bin/sh
set -eu

command_text=${1-}
case "$command_text" in
    *'/etc/init.d/doorfast status'*)
        exit 0
        ;;
    *'ubus call doorfast status'*)
        printf '%s\n' '{"running":true,"runtime_id":"0123456789abcdef","media":{"installed":true,"available":true,"state":"idle","generation":0,"encoder_running":false}}'
        ;;
    *'ubus call doorfast station_scan'*)
        printf '%s\n' '{"runtime_id":"0123456789abcdef","scheduled":true,"frames_sent":3}'
        ;;
    *'ubus call doorfast station_candidates'*)
        printf '%s\n' '{"runtime_id":"0123456789abcdef","candidates":[{"logical_address":"32:02:01:00:02:00","ipv4":"10.2.1.20","first_seen_ms":100,"last_seen_ms":200,"reply_count":3,"configured":true},{"logical_address":"32:02:01:00:03:00","ipv4":"10.2.1.30","first_seen_ms":110,"last_seen_ms":210,"reply_count":2,"configured":true},{"logical_address":"32:02:01:00:04:00","ipv4":"10.2.1.40","first_seen_ms":120,"last_seen_ms":220,"reply_count":1,"configured":false}]}'
        ;;
    *'ubus call doorfast stations'*)
        printf '%s\n' '{"runtime_id":"0123456789abcdef","revision":7,"stations":[{"id":"gate_main","name":"Main Gate","logical_address":"32:02:01:00:02:00","enabled":true,"stream_name":"doorfast_gate_main","route_source":"discovered","route_fresh":true,"monitorable":true,"last_seen_ms":200},{"id":"gate_service","name":"Service Gate","logical_address":"32:02:01:00:03:00","enabled":true,"stream_name":"doorfast_gate_service","route_source":"configured","route_fresh":true,"monitorable":true,"last_seen_ms":210}]}'
        ;;
    *'wget -qO- http://127.0.0.1/cgi-bin/doorfast/api/v1/stations'*)
        printf '%s\n' '{"runtime_id":"0123456789abcdef","revision":7,"stations":[{"id":"gate_main","name":"Main Gate","logical_address":"32:02:01:00:02:00","enabled":true,"stream_name":"doorfast_gate_main","route_source":"discovered","route_fresh":true,"monitorable":true,"last_seen_ms":200},{"id":"gate_service","name":"Service Gate","logical_address":"32:02:01:00:03:00","enabled":true,"stream_name":"doorfast_gate_service","route_source":"configured","route_fresh":true,"monitorable":true,"last_seen_ms":210}]}'
        ;;
    *'uci -q get doorfast.main.gvs_local_address'*)
        printf '%s\n' 'IS:2-1-101-1'
        ;;
    *'uci -q get doorfast.main.multicast_mode'*)
        printf '%s\n' 'auto'
        ;;
    *'uci -q get doorfast.main.multicast_address'*)
        printf '\n'
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

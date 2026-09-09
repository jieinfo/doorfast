#!/bin/sh
set -eu

make peer-sim >/dev/null

build/gvs-peer-sim --scenario no-peer > build/no-peer.jsonl
build/gvs-peer-sim --scenario no-peer > build/no-peer-repeat.jsonl
cmp build/no-peer.jsonl build/no-peer-repeat.jsonl
grep -F '"event":"election_complete","phase":"periodic","role":"maintainer"' build/no-peer.jsonl
grep -F '"destination_scope":"same_apartment","accepted_call":true,"session_state":"ringing"' build/no-peer.jsonl
grep -F '"destination_scope":"other_apartment","accepted_call":false,"session_state":"idle"' build/no-peer.jsonl

build/gvs-peer-sim --scenario lower-peer > build/lower-peer.jsonl
grep -F '"role":"follower"' build/lower-peer.jsonl
grep -F '"online_peers":0,"peer_presence_evidence_gap":true' build/lower-peer.jsonl

build/gvs-peer-sim --scenario maintainer-loss > build/maintainer-loss.jsonl
grep -F '"event":"first_period_missed","role":"follower"' build/maintainer-loss.jsonl
grep -F '"event":"maintainer_takeover","role":"maintainer"' build/maintainer-loss.jsonl

! grep -E '31313131|41414141|random|encrypt|secret|credential' build/*.jsonl

if build/gvs-peer-sim --scenario unknown >/dev/null 2>&1; then
    exit 1
else
    test "$?" -eq 2
fi

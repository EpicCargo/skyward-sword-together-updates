#!/usr/bin/env bash
# Starts a relay on a free port and runs two C clients against it.
set -euo pipefail
cd "$(dirname "$0")/.."
PORT=$((20000 + RANDOM % 20000))
PORT=$PORT node ../server/relay.js > build/relay.log 2>&1 &
RELAY=$!
trap 'kill $RELAY 2>/dev/null' EXIT
sleep 0.5
# Bob starts late: Alice's first connection attempt is alone, then Bob joins
./build/net_e2e $PORT Alice Bob & A=$!
sleep 1
./build/net_e2e $PORT Bob Alice & B=$!
wait $A; RA=$?
wait $B; RB=$?
[ $RA -eq 0 ] && [ $RB -eq 0 ] && echo "e2e: PASS" || { echo "e2e: FAIL"; cat build/relay.log; exit 1; }
./build/rejoin_test "$PORT"

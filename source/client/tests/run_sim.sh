#!/usr/bin/env bash
# Starts a relay on a free port and runs two simulated players walking circles.
set -euo pipefail
cd "$(dirname "$0")/.."
PORT=$((20000 + RANDOM % 20000))
PORT=$PORT node ../server/relay.js > build/relay_sim.log 2>&1 &
RELAY=$!
trap 'kill $RELAY 2>/dev/null' EXIT
sleep 0.5
./build/two_player_sim $PORT Alice Bob & A=$!
sleep 0.5
./build/two_player_sim $PORT Bob Alice & B=$!
RA=0; RB=0
wait $A || RA=$?
wait $B || RB=$?
[ $RA -eq 0 ] && [ $RB -eq 0 ] && echo "sim: PASS" || { echo "sim: FAIL"; cat build/relay_sim.log; exit 1; }

#!/usr/bin/env bash
# Runs the engine inside an invisible gamescope display (headless backend) so tests never take over the
# real screen, keyboard or audio. Each run gets its own data folder (settings, saves, logs).
#   scripts/vtest.sh <run-dir> <seconds> [VAR=value ...] [-- engine args]
# Example: scripts/vtest.sh /tmp/x 30 LARECOMP_SHOT_PATH=/tmp/x/s.png LARECOMP_LAUNCHER_SHOT=90:settings:11
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ $# -lt 2 ]; then
    echo "Usage: scripts/vtest.sh <run-dir> <seconds> [VAR=value ...] [-- engine args]" >&2
    exit 2
fi
DIR=$1; SECS=$2; shift 2
mkdir -p "$DIR/data/skyrecomp/build"
ENVS=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do ENVS+=("$1"); shift; done
[ "${1:-}" = "--" ] && shift
cd "$ROOT/engine"
env XDG_DATA_HOME="$DIR/data" LARECOMP_DATA_ROOT="$DIR/data/skyrecomp" \
    SST_PREVIEW_FILE="$DIR/data/skyrecomp/sst-preview.request" \
    LARECOMP_NO_AUDIO=1 LARECOMP_NO_MOVIE_AUDIO=1 LARECOMP_NO_USER_INPUT=1 "${ENVS[@]}" \
    python3 "$ROOT/scripts/native-test-runner.py" --lock "$ROOT/build/native-test.lock" --seconds "$SECS" \
    --rss-mib "${SST_NATIVE_RSS_MIB:-12288}" \
    -- gamescope --backend headless -W 1280 -H 720 -w 1280 -h 720 -- ./build/skyrecomp "$@" \
    > "$DIR/gamescope.log" 2>&1
RC=$?
[ $RC -eq 124 ] && echo "(stopped after ${SECS}s)"
echo "log: $DIR/data/skyrecomp/skyrecomp.log"
exit $RC

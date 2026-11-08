#!/usr/bin/env bash
# Local development launcher. Each SST_DATA_DIR has its own settings and saves.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GAME_DIR="${SST_GAME_DIR:-$HOME/Downloads/Skysword/SST-game}"
DATA_DIR="${SST_DATA_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/skyward-sword-together}"
if [ ! -f "$GAME_DIR/exefs/main" ] || [ ! -f "$GAME_DIR/romfs.bin" ]; then
    echo "Game files missing in $GAME_DIR. Set SST_GAME_DIR to your prepared v1.0.0 folder." >&2
    exit 1
fi
GAME_DIR="$(realpath "$GAME_DIR")"
if [ ! -x "$ROOT/engine/build/skyrecomp" ]; then
    "$ROOT/scripts/build-engine.sh"
fi
mkdir -p "$DATA_DIR"
DATA_DIR="$(cd "$DATA_DIR" && pwd)"
cd "$ROOT/engine"
exec env LARECOMP_DATA_ROOT="$DATA_DIR" LARECOMP_CC="${LARECOMP_CC:-$(command -v clang)}" \
    ./build/skyrecomp "$GAME_DIR" "$@"

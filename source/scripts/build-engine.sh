#!/usr/bin/env bash
# Build the SkyRecomp engine fork (engine/) with the Skyward Sword Together runtime.
# Header-only deps that CachyOS doesn't install by default are fetched into .deps/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/.deps"
JOBS="${SST_BUILD_JOBS:-1}"
if [ "$JOBS" != 1 ]; then
    echo "Background developer builds require SST_BUILD_JOBS=1." >&2
    exit 2
fi
for ARG in "$@"; do
    case "$ARG" in -j*|--jobs*|MAKEFLAGS=*|MFLAGS=*)
        echo "Build job overrides are disabled; background developer builds use one job." >&2
        exit 2;;
    esac
done
mkdir -p "$DEPS"
[ -d "$DEPS/Vulkan-Headers" ] || git clone -q --depth 1 --branch v1.4.328 https://github.com/KhronosGroup/Vulkan-Headers.git "$DEPS/Vulkan-Headers"
[ -d "$DEPS/VulkanMemoryAllocator" ] || git clone -q --depth 1 --branch v3.1.0 https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git "$DEPS/VulkanMemoryAllocator"
INC="$DEPS/Vulkan-Headers/include:$DEPS/VulkanMemoryAllocator/include"
export C_INCLUDE_PATH="$INC${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
export CPLUS_INCLUDE_PATH="$INC${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"
# Arch/CachyOS ship libshaderc_shared.so; libshaderc.a alone does not link.
exec env MAKEFLAGS= MFLAGS= python3 "$ROOT/scripts/resource_runner.py" \
    --rss-mib 4096 --available-mib 6144 --nice 10 --lock "$ROOT/build/developer-build.lock" \
    -- make -C "$ROOT/engine" -j1 SHADERC=-lshaderc_shared "${@:-build/skyrecomp}"

#!/usr/bin/env bash
# Host-side regressions for the local Linux engine; fixtures stay outside the checkout.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/sst-engine-tests.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
TEST_CC="${CC:-clang}"
cd "$ROOT/engine"
FLAGS=(-O1 -Wno-format -ffunction-sections -fdata-sections -Iruntime -Wl,--gc-sections -lpthread)
"$TEST_CC" "${FLAGS[@]}" tools/test_build_resume.c -o "$WORK/build-resume"
(cd "$WORK" && ./build-resume)
"$TEST_CC" "${FLAGS[@]}" tools/test_condvar_publish.c -o "$WORK/condvar"
timeout 10 "$WORK/condvar"

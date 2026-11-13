# SST source snapshot

This snapshot contains the capacity8 client, relay and launcher sources, the gameplay9 travel UI fixes, and additional native prop-binding fixes. Patch29 passed CPU checks, an isolated Windows runtime cross-build and one native Windows game with three scalar peers and an independent pot census. Protocol **45**, Launcher **5**, SkyRecomp **2.0.2**.

The current source candidate is represented by 43 patches against upstream commit `506a9bc5aead5fc9598bb8f4a521f3b26f8897e8`. Patch39 replaces an oversized whole-heap Vulkan descriptor with a small address/size uniform and bounded addressable-buffer access, while rejecting malformed shader operands before fixed-array indexing. Patch40 repairs the manual clean-rebuild recipe so it generates and links all four game modules at the runtime loader's exact bases. Patch41 aligns the guest crash symbolizer with that same four-module map. Patch42 preflights Vulkan 1.3, buffer-device-address and presentation-queue requirements before selecting an adapter. Patch43 initializes dynamic stencil state on disabled-stencil draws after a real WSL validation run exposed missing-state errors. The private game-library audit confirms that all four modules and 1,422 link inputs were already recompiled; regenerating them did not fix the WSL title visuals. Patch39 has asset-free compiler coverage but no game launch or visual acceptance. Patch32 covers native simulation resuming between paused synchronization ticks and passed four focused Windows runs. Cutscene/room/death, reconnect, independent clients and campaign readiness remain pending. The accepted local travel9 portable remains pinned to its earlier 28-patch source and runtime. Generated game code and compiled upstream dependencies are not included.

## Reconstruct the engine

From this `source` directory, run:

```sh
python3 scripts/verify-source.py
python3 scripts/restore-engine.py
```

Restoration tries the original upstream, then a public fork containing the exact pinned base commit. It imports the patches in a new checkout and verifies the exact tracked engine tree and 255 frozen native source files; a fork's current runtime is never substituted. It refuses to replace an existing `engine/` folder. For offline restoration, pass `--repository /path/to/an/existing/SkyRecomp/repository` containing the pinned commit. This overrides the defaults; repeat `--repository` to supply your own ordered alternatives. Failed retrievals leave no partial engine. Tree and native-file validation remain active under optimized Python.

`scripts/build-engine.sh` and `scripts/build-windows.py` provide guarded builds after their public prerequisites are installed. Windows compiler/support preparation is in `scripts/fetch-windows-compiler.py` and `scripts/fetch-windows-support.py`. Builds use one compiler and monitored memory reserves. See [development resource policy](DEVELOPMENT-RESOURCE-POLICY.md).

The [Windows runtime CI job](docs/windows-runtime-ci.md) cross-builds the current runtime on a separate Ubuntu runner using frozen public source headers and a pinned LLVM-MinGW archive. Its artifact is for isolated validation and includes exact source/binary receipts and licenses. It launches no game and does not update an installation, portable package or public release feed.

The pinned upstream Windows support ZIP returned HTTP 404 during the latest continuation. Completed verified support caches remain reusable. A new [offline cache importer](docs/windows-support-cache.md) restores a separately checksummed dependency backup into a matching checkout, verifies every archived file and preserves existing files. Its real-cache integration recovered all 85 support files without HTTP. Fresh prerequisite retrieval without such a backup remains an open blocker; the engine-source fallback above does not replace release DLLs or setup tools. See [remaining work](COOP-REMAINING.md).

## Tests and evidence

Two patch42 Linux game captures reached frame 1,200 and exited cleanly; the title UI and scenery are still incomplete. Patch43 corrects a stencil-state initialization issue found during that comparison. Post-fix native validation remains pending. See [November 13 changes](CHANGES-2026-11-13.md) and `validation/wsl-visual-stencil-20261113.json`.

The current 42-patch renderer passes 14 real offscreen Vulkan guest-memory readback cases with zero validation warnings, including wide stores at the heap boundary and memory access beyond the driver's 128 MiB descriptor limit. The [reproducible fixture](docs/vulkan-guest-memory-test.md) and exact source receipt are included. Game visuals remain pending.

```sh
python3 -m unittest discover -s launcher/tests -v
python3 -m unittest discover -s scripts/tests -v
```

Some networking tests require Node.js, OpenSSL and a compiled `client/build/sst-session-host`. They skip explicitly when those prerequisites are absent. Native client tests are defined in `client/Makefile`; they use synthetic game state and local peers.

GitHub's [source and co-op checks](../.github/workflows/source-checks.yml) build the native helper and host fixture before running these suites, and also verify engine restoration and the room Worker tests. [November 11 changes](CHANGES-2026-11-11.md) describe the portable-seeding and Windows publication fixes and their focused regression checks.

The default Windows source ZIP includes this snapshot's complete checksum inventory. After restoring `engine/`, run `python3 scripts/check-source-archive.py` to verify the generated archive and restore the exact engine from its contents. This check launches no compiler or game.

[Changes and validation scope](CHANGES-2026-11-08.md) distinguish CPU/network checks from actual hardware gameplay. [November 13 changes](CHANGES-2026-11-13.md) record the recompilation audit and renderer foundation. The `validation/` directory contains frozen source identities, aggregate test results, capture hashes and the representative carry replay plan. Game screenshots, game libraries, generated game code and personal profiles are excluded.

## Layout

- `client/`: co-op networking, state, pose and prop services, with tests.
- `server/`: standalone relay and opaque-room Worker sources.
- `launcher/`: setup/update launcher and online-room transport, with tests.
- `engine-patches/`: complete ordered engine patch series, including travel UI and prop-binding fixes.
- `scripts/`: source restoration, build, package and test tools.
- `tests/`: actual bridge CPU fixture; `validation/`: extra capacity regressions and evidence.
- `docs/`: existing protocol, hosting and world-sync reference material. Older platform-validation statements there are superseded by this snapshot's dated validation record.

The local travel9 portable candidate now passed its final Windows package check; the earlier capacity8 candidate is preserved. Publishing this source does not change the release feed or install a runtime. Separate-PC/campaign readiness, other carryable classes, throws, and the WSL software-Vulkan rendering issue remain open.

A later baseline Windows run independently sampled the same unique pot actor through genuine pickup/putdown, moving remote carry, expiry/context restoration and remote placement. Its 77 valid hall samples retained one actor lifetime. This used the earlier travel9 runtime and three scalar peers; it does not establish patch29 or separate-full-client gameplay acceptance.

The newly built patch29 runtime subsequently passed the same full sequence: 97 stable hall samples retained one unique pot actor, all eight replay phases had independent position samples, and owned processes/sockets closed normally. See `validation/patch29-live-pot-census-20261111.json`. Separate full clients and campaign completion remain untested.

Upstream engine licensing is preserved in `ENGINE-LICENSE`; third-party source licenses remain alongside their respective files.

Two focused patch32 Windows runs subsequently passed with one full game and three scalar peers each. Both retained one pot actor across 97 stable hall samples and exited normally with owned children/sockets closed and private-stage files restored. The first confirmed sharing-off floor restoration; the second confirmed zero clear-floor settling shift and native re-pickup/putdown with outgoing acquisition 2. See `validation/patch32-native-sharing-20261111.json` and `validation/patch32-native-placement-20261111.json`. This does not establish actual pause/cutscene/disconnect, other object classes, independent full clients or campaign readiness.

Two additional patch32 runs verified native Gear pause flags `[1, 1, 1]`, saved-floor recovery after resume, sharing-off while Gear-paused, and native own-client Disconnect while Gear-paused followed by floor recovery. The disconnect fixture closes its three sockets before a bounded wait for the final capture; unexpected EOF, missing capture and malformed protocol still fail. See `validation/patch32-native-pause-20261111.json`, `validation/patch32-native-disconnect-20261111.json` and [disconnect observation](docs/native-disconnect-observation.md). These remain one full game with three scalar peers per run.

A later full-replay attempt using the new `6ff80f7` CI runtime was stopped at Windows process creation by Smart App Control. Code Integrity events 3077/3118 identify its exact hash `84c6cbbbbac16ae1e8ffb60f340d850f9065e1135a630fb5aa91b733b8464975`. No game child launched and the original private stage was restored. That binary has no full-replay or portable acceptance; the earlier focused records retain their exact tested runtime hashes. See `validation/windows-runtime-execution-block-20261111.json`.

Remote scene boundaries now retire accepted prop targets and movement history even when the player reloads the same area or returns between bridge updates. The new receiver change passed 36 scalar, 18 actual TCP and 54 actual bridge departure/re-entry cases, with exact prior-receiver reproduction. See [November 12 changes](CHANGES-2026-11-12.md) and `validation/scene-incarnation-20261112.json`. This client change has no new Windows gameplay acceptance.

Patch33 preserves only a prior remote carry’s pending floor cleanup when bounded actor discovery fails. The exact actor callback still validates native identity, current player/scene, held state and model before a one-time restoration. The candidate passed all 84 hosted discovery-loss cases; the exact patch32 baseline reproduces 36 failures with 48 passing controls. See `validation/discovery-cleanup-20261112.json`. Native gameplay remains pending for this candidate.

Patch34 admits remote prop records only for exact current native bindings, reclaims retired keys, and retires cached transforms on actor replacement while preserving live-key replay watermarks. A local receive-time fence rejects pre-binding queued packets, including clock ties; first discovery still accepts fresh earlier arrivals. It passed 72 focused cases and 600,000 full-table replacements while retaining 31 stable placements per case. See [November 12 changes](CHANGES-2026-11-12.md) and `validation/native-admission-20261112.json`. Native gameplay for these changes remains pending.

Patch35 retires stale session authority in the executing actor callback when reconnect completes before the next world synchronization tick. It prevents old placements and premature sends while retaining validated floor cleanup and native held linkage. Its 60 CPU cases pass against 24 failures and 36 controls from the exact patch34 baseline; native gameplay remains pending. See `validation/actor-session-20261112.json`.

Patch36 checks the current local player and area at prop actor callbacks and rechecks after ordinary native execution. Old placements and carries cannot cross a local player reload or room/stage/layer boundary before the next world tick; post-execution connection changes cannot publish an old-session observation. All 96 CPU cases pass against 60 failures and 36 controls from the exact patch35 baseline. Native gameplay remains pending. See `validation/actor-context-20261112.json`.

Patch37 refreshes the current authenticated peer roster at actor callbacks, including after ordinary native execution. A departed or reloaded peer loses cached placement/carry authority before the next world tick. Invalid rosters retain only independently validated once-only floor cleanup. Its 108 CPU cases pass against 69 failures and 39 controls from the exact patch36 header; native gameplay remains pending. See `validation/actor-owner-20261112.json`.

Patch38 revalidates native identity and presentation authority after each returning model guest call and before committing velocity or applied stamps. Interrupted presentations stop their tail and skip duplicate native execution; partially presented carries retain independently validated once-only floor cleanup. All 196 model-boundary CPU cases pass against 156 failures and 40 controls from the exact patch37 bridge and native adapter. Native gameplay remains pending. See `validation/model-boundaries-20261112.json`.

Patch39 fixes a Vulkan descriptor-limit violation in the WSL/software-renderer path. Shader global-memory accesses now use a 16-byte address/size uniform, validate complete access spans and retain a safe zero-backed fallback. The engine translator also bounds register and constant-buffer operands. Ten thousand MSL comparisons, 2,000 GLSL programs, 12 direct boundary cases and changed runtime-object compilation pass without launching the game. The existing private game library already contains all 1,422 expected link inputs. See [November 13 changes](CHANGES-2026-11-13.md) and `validation/recompilation-renderer-foundation-20261113.json`. Native visual acceptance remains pending.

Patch40 makes `make gen` and `make gen-objs` agree with the runtime and the Python game builder: `main`, `sdk`, `subsdk0` and `subsdk1` are generated and linked, and their bases are `0x7000000000`, `0x7030000000`, `0x7032000000` and `0x7034000000`. A regression test compares the Makefile directly with `runtime/rt.h`; the guarded test and a four-command dry run pass. This fixes future clean/manual rebuilds and does not claim that recompilation fixes the remaining rendering problem.

Patch41 updates the developer crash symbolizer from its stale two-module map and old SDK base to all four runtime modules. The hosted build-tool regression now also derives the expected symbolizer map from `runtime/rt.h`, preventing incorrect SDK offsets or unknown `subsdk0`/`subsdk1` frames from silently returning.

Patch42 prevents a preferred but incompatible Vulkan adapter from being selected ahead of a usable fallback. Candidate devices must expose Vulkan 1.3, buffer device addresses and one graphics+compute+present queue before type or `LARECOMP_GPU` preference is scored. Eight sanitizer-backed selection cases, the modified real source compilation and a read-only WSL capability probe pass without creating a logical device or launching the game. See `validation/vulkan-device-selection-20261113.json`.

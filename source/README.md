# SST source snapshot

This snapshot contains the capacity8 client, relay and launcher sources, the gameplay9 travel UI fixes, and additional native prop-binding fixes. Patch29 passed CPU checks, an isolated Windows runtime cross-build and one native Windows game with three scalar peers and an independent pot census. Protocol **45**, Launcher **5**, SkyRecomp **2.0.2**.

The current source candidate is represented by 32 patches against upstream commit `506a9bc5aead5fc9598bb8f4a521f3b26f8897e8`. Patch32 covers native simulation resuming between paused synchronization ticks, with exact-baseline reproduction and 42 pause/resume cases alongside 138 deactivation cases in the hosted workflow. Its Windows runtime cross-build and four focused native Windows checks passed: sharing-off floor restoration, same-actor clear-floor handback, Gear pause recovery and paused own-client Disconnect. Cutscene/room/death and network reconnect handling remain pending; the exact between-tick race is covered by CPU regressions. The earlier patch30 Windows runtime completed eight replay phases before a memory-reserve stop, without full gameplay acceptance. The complete eight-phase replay sequence remains recorded for patch29; patch32’s two focused checks are recorded separately. The accepted local travel9 portable remains pinned to its earlier 28-patch source and runtime. Generated game code and compiled upstream dependencies are not included.

## Reconstruct the engine

From this `source` directory, run:

```sh
python3 scripts/verify-source.py
python3 scripts/restore-engine.py
```

Restoration tries the original upstream, then a public fork containing the exact pinned base commit. It imports the patches in a new checkout and verifies the exact tracked engine tree and 253 frozen native source files; a fork's current runtime is never substituted. It refuses to replace an existing `engine/` folder. For offline restoration, pass `--repository /path/to/an/existing/SkyRecomp/repository` containing the pinned commit. This overrides the defaults; repeat `--repository` to supply your own ordered alternatives. Failed retrievals leave no partial engine. Tree and native-file validation remain active under optimized Python.

`scripts/build-engine.sh` and `scripts/build-windows.py` provide guarded builds after their public prerequisites are installed. Windows compiler/support preparation is in `scripts/fetch-windows-compiler.py` and `scripts/fetch-windows-support.py`. Builds use one compiler and monitored memory reserves. See [development resource policy](DEVELOPMENT-RESOURCE-POLICY.md).

The [Windows runtime CI job](docs/windows-runtime-ci.md) cross-builds the current runtime on a separate Ubuntu runner using frozen public source headers and a pinned LLVM-MinGW archive. Its artifact is for isolated validation and includes exact source/binary receipts and licenses. It launches no game and does not update an installation, portable package or public release feed.

The pinned upstream Windows support ZIP returned HTTP 404 during the latest continuation. Completed verified support caches remain reusable. A new [offline cache importer](docs/windows-support-cache.md) restores a separately checksummed dependency backup into a matching checkout, verifies every archived file and preserves existing files. Its real-cache integration recovered all 85 support files without HTTP. Fresh prerequisite retrieval without such a backup remains an open blocker; the engine-source fallback above does not replace release DLLs or setup tools. See [remaining work](COOP-REMAINING.md).

## Tests and evidence

```sh
python3 -m unittest discover -s launcher/tests -v
python3 -m unittest discover -s scripts/tests -v
```

Some networking tests require Node.js, OpenSSL and a compiled `client/build/sst-session-host`. They skip explicitly when those prerequisites are absent. Native client tests are defined in `client/Makefile`; they use synthetic game state and local peers.

GitHub's [source and co-op checks](../.github/workflows/source-checks.yml) build the native helper and host fixture before running these suites, and also verify engine restoration and the room Worker tests. [November 11 changes](CHANGES-2026-11-11.md) describe the portable-seeding and Windows publication fixes and their focused regression checks.

The default Windows source ZIP includes this snapshot's complete checksum inventory. After restoring `engine/`, run `python3 scripts/check-source-archive.py` to verify the generated archive and restore the exact engine from its contents. This check launches no compiler or game.

[Changes and validation scope](CHANGES-2026-11-08.md) distinguish CPU/network checks from actual hardware gameplay. The `validation/` directory contains frozen source identities, scalar test results, capture hashes and the representative carry replay plan. Game screenshots, game libraries, generated game code and personal profiles are excluded.

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

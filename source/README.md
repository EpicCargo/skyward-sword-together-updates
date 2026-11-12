# SST source snapshot

This snapshot contains the capacity8 client, relay and launcher sources, the gameplay9 travel UI fixes, and additional native prop-binding fixes. Patch29 passed CPU checks, an isolated Windows runtime cross-build and one native Windows game with three scalar peers and an independent pot census. Protocol **45**, Launcher **5**, SkyRecomp **2.0.2**.

The current source candidate is represented by 32 patches against upstream commit `506a9bc5aead5fc9598bb8f4a521f3b26f8897e8`. Patch32 covers native simulation resuming between paused synchronization ticks, with exact-baseline reproduction and 42 pause/resume cases alongside 138 deactivation cases in the hosted workflow. Its Windows build and gameplay remain pending. The earlier patch30 Windows runtime completed eight replay phases before a memory-reserve stop, without full gameplay acceptance. The latest fully accepted gameplay evidence remains patch29. The accepted local travel9 portable remains pinned to its earlier 28-patch source and runtime. Generated game code and upstream build dependencies are not included.

## Reconstruct the engine

From this `source` directory, run:

```sh
python3 scripts/verify-source.py
python3 scripts/restore-engine.py
```

Restoration tries the original upstream, then a public fork containing the exact pinned base commit. It imports the patches in a new checkout and verifies the exact tracked engine tree and 253 frozen native source files; a fork's current runtime is never substituted. It refuses to replace an existing `engine/` folder. For offline restoration, pass `--repository /path/to/an/existing/SkyRecomp/repository` containing the pinned commit. This overrides the defaults; repeat `--repository` to supply your own ordered alternatives. Failed retrievals leave no partial engine. Tree and native-file validation remain active under optimized Python.

`scripts/build-engine.sh` and `scripts/build-windows.py` provide guarded builds after their public prerequisites are installed. Windows compiler/support preparation is in `scripts/fetch-windows-compiler.py` and `scripts/fetch-windows-support.py`. Builds use one compiler and monitored memory reserves. See [development resource policy](DEVELOPMENT-RESOURCE-POLICY.md).

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

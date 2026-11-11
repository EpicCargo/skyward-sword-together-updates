# SST source snapshot

This snapshot contains the capacity8 client, relay and launcher sources, the hardware-validated gameplay9 travel UI fixes, and additional CPU-checked native prop-binding fixes. The binding fixes still need a full runtime build and hardware gameplay check. Protocol **45**, Launcher **5**, SkyRecomp **2.0.2**.

The current engine is represented by 29 patches against upstream commit `506a9bc5aead5fc9598bb8f4a521f3b26f8897e8`. The accepted local travel9 portable remains pinned to its earlier 28-patch source and runtime. Generated game code and upstream build dependencies are not included.

## Reconstruct the engine

From this `source` directory, run:

```sh
python3 scripts/verify-source.py
python3 scripts/restore-engine.py
```

Restoration clones the pinned public upstream source, imports the patches in a new checkout, and verifies the exact tracked engine tree and 253 frozen native source files. It refuses to replace an existing `engine/` folder. For offline restoration, pass `--repository /path/to/an/existing/SkyRecomp/repository` containing the pinned commit.

`scripts/build-engine.sh` and `scripts/build-windows.py` provide guarded builds after their public prerequisites are installed. Windows compiler/support preparation is in `scripts/fetch-windows-compiler.py` and `scripts/fetch-windows-support.py`. Builds use one compiler and monitored memory reserves. See [development resource policy](DEVELOPMENT-RESOURCE-POLICY.md).

## Tests and evidence

```sh
python3 -m unittest discover -s launcher/tests -v
python3 -m unittest discover -s scripts/tests -v
```

Some networking tests require Node.js, OpenSSL and a compiled `client/build/sst-session-host`. They skip explicitly when those prerequisites are absent. Native client tests are defined in `client/Makefile`; they use synthetic game state and local peers.

GitHub's [source and co-op checks](../.github/workflows/source-checks.yml) build the native helper and host fixture before running these suites, and also verify engine restoration and the room Worker tests. [November 11 changes](CHANGES-2026-11-11.md) describe the portable-seeding and Windows publication fixes and their focused regression checks.

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

Upstream engine licensing is preserved in `ENGINE-LICENSE`; third-party source licenses remain alongside their respective files.

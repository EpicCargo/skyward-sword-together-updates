# Linux runtime build validation

The [Linux workflow](../../.github/workflows/linux-runtime-validation.yml) builds the asset-free x86-64 runtime on a separate Ubuntu 24.04 runner. This lets development prepare a Linux executable while the gaming PC is below its local full-build memory reserve. Runtime execution still requires the ordinary game-test guards.

The original v2.0.2 public AppImage download currently returns HTTP 404. The existing local dependency cache was verified against that AppImage's pinned SHA256. Its exported C interfaces and version nodes are recorded in `third_party/linux-link-interfaces-v202.json`; the existing frozen public source headers provide matching declarations. No game data or dependency implementation bytes are included in that metadata.

The worker generates versioned ELF shared objects solely for linking, preserving each real library's SONAME, symbol version, weak binding and data size. Their function bodies contain a trap. They are never executed, tested as implementations or included in the artifact. This uses the same separation between interface and implementation described by [LLVM's shared-object stubbing tools](https://llvm.org/docs/CommandGuide/llvm-ifs.html) and [GNU symbol versioning](https://sourceware.org/binutils/docs/ld/VERSION.html).

The final executable requests the real `libSDL3.so.0`, FFmpeg 63/61/10 and `libshaderc_shared.so.1` dependencies. Static validation rejects an embedded library search path and imports outside the frozen versioned interfaces. Actual execution must resolve the real libraries identified by SHA256 in `BUILD-RECEIPT.json`. This does not supply a fresh-machine replacement for the missing public dependency download.

The build uses one compiler, nice level 10, a 4096 MiB owned-group RSS cap and a 6144 MiB free-memory reserve. Contract fixtures use the smaller 512 MiB cap with the same hosted reserve. Real ELF fixtures check versioned imports, SONAMEs, weak symbols and data sizes without executing their trap functions. The engine tree, every frozen native hash and the source inventory are checked before/after the build.

The artifact contains only the runtime, static receipts and license notices. It contains no game library, game files, personal saves or link-interface libraries. No runtime/game is launched in CI; an actual guarded WSL game run remains a separate acceptance step. No installation or release feed is changed.

# Windows runtime CI

The `windows-runtime` job runs after the source and co-op tests on pushes to main
or manual workflow runs. It cross-builds only `skyrecomp.exe` and its import
library on a separate Ubuntu runner. Pull requests run the source tests without
publishing a runtime artifact.

The build reconstructs the exact engine tree and checks all 253 native source
hashes. Frozen public headers, import definitions and their licenses live in
`third_party/windows-build-headers-v202`; their provenance and complete inventory
are included. No compiled dependency libraries or game files are in that source
directory. LLVM-MinGW 20261020 is downloaded from its upstream release with a
pinned archive SHA256 and byte count. Import libraries are regenerated with
`llvm-dlltool` before the direct runtime target is built.

The cloud build uses one compiler, niceness 10, a 4096 MiB group RSS cap and a
6144 MiB available-memory reserve on its Ubuntu VM. Local Windows/Linux build
preflights and limits remain in force on the development PC. The worker refuses
ordinary local invocation; `--validate-inputs` performs file-only verification.

The seven-day CI artifact contains the runtime, import library, notices, a build
receipt and a complete SHA256 inventory. The receipt ties the binary to the
source commit, source inventory, engine tree and compiler archive. It also records
the developer-counter RVA derived from that binary's symbols and PE ImageBase;
independently verify the exact executable and counter address before observation.

This artifact is an isolated validation build, not a playable portable package.
It contains no runtime dependency DLLs, compiler distribution, game library,
game assets or saves. It is not installed or added to a public Release or update
feed. A successful cross-build does not establish native gameplay acceptance.
Fresh playable-package dependencies still require a verified cache or replacement
for the unavailable upstream support download.

File-only regression coverage checks SDK integrity, traversal, duplicate keys,
symlinks, bounded and pinned downloads, preservation of existing files, compiler
archive containment, local-worker refusal and AMD64 PE metadata parsing. No
compiler, Windows executable, game or real HTTP request is used by these tests.

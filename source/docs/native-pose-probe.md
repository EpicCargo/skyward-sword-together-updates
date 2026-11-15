# Private native pose observations

The optional pose diagnostic observes admitted local rendering inside the game
process. It records the current model/palettes before an animation-cache hit can
return, allowing later comparison with the independent resource evaluator. It
does not generate a pose, render a replacement, change native state or teach the
cache from a forced viewer. Normal builds leave it disabled.

Set `LARECOMP_DEBUG_DATA=1`, `SST_POSE_PROBE=1` and an absolute
`SST_POSE_PROBE_DIR` before starting an isolated test profile. The directory must
exist. The diagnostic exclusively creates `native-poses.bin`; an existing file is
never overwritten. With the flags unset, it performs no file operations. Raw
records are private game-derived data and must stay out of GitHub, source ZIPs,
release artifacts and public evidence. Publish scalar errors/counts only.

The recorder admits only ordinary clip26/action0 or steady small-carry
clip26/action67. It uses the existing genuine-source readiness gate. Declared
forced-viewer draws never invoke it. Metadata reads require the exact configured
player vtable, published actor/instance/scene, pose position/rotation/action,
primary clip/phase and two equal snapshots of the six observed tracks. Bounds are
checked before those in-process reads. `SST_NO_PLAYER_READ` and missing diagnostic
flags reject before reading native objects. The held pointer is a presence value
and is never dereferenced.

Track IDs, phases and durations are observed. Blend weights are **not** observed
or inferred. Comparisons that choose weights must identify them as controlled
hypotheses. A matching steady slot5 clip431 at4.5/9 is a scope gate, not proof of
all native blend weights or callbacks.

The file starts with `SSTP56\0\0`, little-endian version1 and endian tag0x01020304.
Each fixed160-byte `SstProbeRecord` describes its bounded position/normal payload,
frame/mesh/context, clip/phase, track metadata, scene and canonical model matrix.
Position and normal matrices follow as float32. The existing actor-origin/yaw and
camera transform is removed; normal conversion omits translation. No native
pointers are recorded. One coherent set of parts is sampled at most every60
presented frames, with at most16 sampled frames per context,64 mesh identities,
1,024 records and32MiB total. Nonfinite, inconsistent and out-of-scope data are
skipped. Capacity or write failure stops recording without affecting rendering.

The synthetic recorder fixture passes5,937 assertions; the in-process track
reader fixture passes49. Checks cover strict opt-in, unchanged inputs, cache-hit
observation without extra stored palettes, camera canonicalization, forced-viewer
denial, metadata identity/bounds, inconsistent part rejection, independent context
caps, the1,024-record limit and exclusive output. Existing animation, fresh-local,
carry-cohort, geometry-only and phase-admission regression fixtures also pass under
address, undefined-behavior and leak sanitizers. These are CPU checks, not native
carry-pose acceptance. See [CPU evidence](../validation/pose-probe-20261115.json).

Native capture and comparison will be recorded separately with exact runtime
hashes and limits. Cold rendering integration, actual native weights, callbacks,
hand-shape geometry and independent-client gameplay acceptance remain unfinished.

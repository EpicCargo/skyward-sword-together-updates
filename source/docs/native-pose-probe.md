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
1,024 records and32MiB total. Parts in one sampled frame must also retain the
same local tick, position, rotation, clip, phase, action and equipment identity.
Nonfinite, inconsistent and out-of-scope data are
skipped. Capacity or write failure stops recording without affecting rendering.

The initial diagnostic revision's synthetic recorder fixture passes5,937 assertions; the in-process track
reader fixture passes49. Checks cover strict opt-in, unchanged inputs, cache-hit
observation without extra stored palettes, camera canonicalization, forced-viewer
denial, metadata identity/bounds, inconsistent part rejection, independent context
caps, the1,024-record limit and exclusive output. Existing animation, fresh-local,
carry-cohort, geometry-only and phase-admission regression fixtures also pass under
address, undefined-behavior and leak sanitizers. These are CPU checks, not native
carry-pose acceptance. See [initial CPU evidence](../validation/pose-probe-20261115.json),
which records its exact source hashes. The additional same-frame pose guard passes
5,993 recorder assertions and49 reader assertions, plus the same five regression
fixtures, under local address, undefined-behavior and leak sanitizers with strict
compiler warnings. Source tests and Windows/Linux builds also pass on the hosted
candidate. See [coherence check evidence](../validation/pose-probe-coherence-20261115.json)
for exact code hashes; the initial evidence remains scoped to its earlier revision.

A finite native run completed7,200 frames in a fresh private profile, including
normal pickup of an academy-hall pot. It captured480 records across16 idle and16
steady-carry frames, with15 part identities per frame and no validation errors.
The actual C evaluator was compared with those captures under address,
undefined-behavior and leak sanitizers. An independent Python evaluator agrees
with its body error maxima. Both use a controlled one-track-per-joint recipe;
actual native blend weights remain unobserved.

For steady carry, the largest absolute position-palette element error is2.98140335
and the largest normal linear-element error is0.00790379569. Rigid hand model
translation errors against generated hand joints are0.0838088989 left and
0.0984344482 right, with linear errors0.000409781933/0.000296235085. These are game
coordinate and matrix-element errors, not pixel distances. Idle captures include
scene and movement settling, with larger errors up to17.5677509; they must not be
reported as a steady idle match. Joint diagnostics locate prominent lower-body
differences around knees/feet, but do not establish their cause. Weights,
callbacks, controller settling and model-specific work remain candidates to
investigate. No generated pose is accepted or rendered from this comparison.
See [native comparison evidence](../validation/pose-probe-native-20261115.json)
for exact runtime/source hashes, counts, errors and resource limits. The raw
capture, resources, screenshots and profile remain private.

 Cold rendering integration, actual native weights, callbacks,
hand-shape geometry and independent-client gameplay acceptance remain unfinished.

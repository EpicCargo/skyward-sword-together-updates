# Private native control observations

The optional pose diagnostic can also record the native head, waist, torso and
hair control fields. This supplies observed inputs for later callback comparison;
it does not apply corrections or enable replacement rendering.

The field layout was identified in the owned HD1.0.0 executable, rather than
assuming the Wii member offsets. The body timing-A code reads head Z/Y/X angles
at0x1a82/0x1a84/0x1a86 and waist Z/Y at0x1a54/0x1a56. Body timing-B uses the torso
angles at0x1a88/0x1a8a/0x1a8c. Head timing-B uses five hair X angles followed by
five Z angles at0x1a58, a hair yaw at0x1a76, and four x/y/z/w quaternions at0x1b20.
These routines obtain the player through their callback's player pointer and
also use the previously verified player yaw/action fields. See
[static layout evidence](../validation/native-controls-layout-20261115.json)
for the owned input/build hashes, function-span hashes and reference addresses.
Raw executable bytes/disassembly remain private. Static identification alone
is not runtime callback or pose acceptance.

Add `SST_POSE_CONTROLS=1` to the existing strict opt-in:
`LARECOMP_DEBUG_DATA=1`, `SST_POSE_PROBE=1`, and an absolute `SST_POSE_PROBE_DIR`.
The directory must already exist. A complete configured control layout is
required; zero/missing offsets reject before those reads. Existing published
player/vtable/instance/pose/scene checks, in-process bounds and two matching
snapshots remain required. `SST_NO_PLAYER_READ` denies reads first. The diagnostic
reads direct fields only; no new native pointers are followed and no native
state is written. All modes remain disabled by default.

Control mode exclusively creates `native-poses-controls.bin`, leaving legacy
`native-poses.bin` and its version1 layout available with the control flag unset.
The new file header is `SSTC58\0\0`, little-endian version2 and tag0x01020304.
Each record contains the same160-byte pose header, then a104-byte control payload,
then position/normal matrices. The record's total byte count includes the payload.
The payload is20 signed16-bit angles (waist2, head3, torso3, hair10, hair yaw1, actor yaw1),
then16 float32 quaternion components in x/y/z/w order.
Actor yaw is the already-validated player pose yaw; it preserves the reference
needed to compare world-space corrections after actor-yaw canonicalization.
Angles retain native signed-angle units; the recorder does not convert them.

Quaternion components must be finite and in[-2,2]. The recorder permits an unused
zero quaternion state and does not normalize it or claim it is a proper rotation.
Any comparison applying a quaternion needs its own applicability/unit checks.
Parts in a sampled frame must retain identical control bytes as well as the
existing coherent pose/track metadata. The16 samples per context,60-frame interval,
1,024-record and32MiB limits remain. Inputs are immutable; forced-viewer draws
cannot teach or invoke the observer. Output is never overwritten.

The extended CPU fixtures pass6,130 recorder and68 reader assertions, including
legacy format, opt-in, bounded control layout, signed values, invalid components,
zero raw quaternion state, control coherence and exclusive version2 output.
Animation/fresh-local/carry-cohort/geometry-only/phase-admission regressions also
pass under address, undefined-behavior and leak sanitizers. See
[CPU evidence](../validation/native-controls-cpu-20261115.json).

These fields do not include native layer blend weights, all model/controller
flags, limb correction pointer targets, arm-Z virtual getter results, or skirt
controller values. Capturing them is not a complete pose recipe. Native capture
and quantitative callback comparison must be recorded separately, with exact
runtime hashes and explicit remaining hypotheses. Raw controls, matrices, models,
animations, profiles, screenshots and saves stay private and out of GitHub/source
archives. Publish only scalar errors/counts/hashes. Cold rendering remains disabled.

A fresh native run completed7,200 frames and normal pot pickup in479.04 seconds,
with480 version2 records across16 idle/settling and16 steady-carry frames. It
reported no Vulkan validation errors and cleaned up its owned processes. Nonzero
head controls occurred in5 idle samples; hair-angle controls in2 idle/1 carry
samples. All32 samples contained nonidentity hair quaternions and nonzero actor
yaw. Waist/torso controls remained zero, so their nonzero behavior is unverified.

The actual public C resource/world math, under sanitizers, was used in a private
callback comparison. An independent evaluator agrees with its reported errors
within3e-5. Applying the recorded head/hair controls reduces the following worst
absolute position-palette element errors (game coordinates, not pixels):

| Samples | Selected-track baseline | Observed head/hair controls | Observed body-head parent plus hair controls |
| --- | ---: | ---: | ---: |
| Idle/settling16 | 11.7682419 | 0.133779526 | 0.00168895721 |
| Steady carry16 | 0.367022514 | 0.0948143005 | 0.000940322876 |

The last column is a hybrid diagnostic using a captured parent joint. It does
not prove independent body-head reconstruction. Native signed-angle rotation
order, actor-yaw conjugation and timing-B parent/child propagation are applied
explicitly; no controls were fitted to the output. The native child animation
frame remains unobserved; the child frame0 and selected body layer weights are
controlled hypotheses. Center/other body callbacks, ArmZ getters, limb/skirt
state and actual blend weights remain missing. No generated pose is accepted or
rendered. See [native evidence](../validation/native-controls-native-20261115.json)
for exact runtime/source/capture hashes, coverage, residuals and resource limits.

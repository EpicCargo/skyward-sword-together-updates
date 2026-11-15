# Masked animation layers and linked joints

The independent model evaluator now supports six ordered animation layers and
absolute joint links for separate models. It still has no renderer callers.
Cold carry requires live layer-state observation, the complete geometry manifest
and a genuine native pose comparison before this path can be enabled.

## Layer rules

`sst_model_layers` accepts borrowed bindings of the same model. Each layer has a
frame, finite nonnegative weight (at most1e6), and an optional binary joint mask.
There are at most six layers. Frames are checked for nonzero-weight layers; a
null binding can represent an empty zero-weight slot without a mask. Caller-owned
loop timing, masks and weights are explicit inputs.

Weights are accumulated separately at each joint. Masked joints, missing tracks
and zero-weight layers do not contribute. With no contributors, the joint uses
model bind S/R/T. A single contributor retains its native FIdx local rotation
without a quaternion round trip. Multiple contributors convert local rotations
to quaternions and apply ordered shortest-hemisphere slerp. Each successive layer
uses its weight divided by the accumulated weight. Translation uses per-joint
normalized weights. Three or more rotations can depend on layer order; they are
not replaced by an unordered quaternion average or Euler interpolation.

Model-default flags are combined before patching. A default rotation/translation
is restored from the bind pose only if all contributing channels request it.
During blending, a default channel supplies raw identity/zero, matching resource
evaluation before the native node patch. Pre-patching every layer would change
the result: a default bind translation10 blended with an explicit translation2
produces1 under the native rule, rather than6. Tests also distinguish this rule
with a rotated bind pose.

Every active input scale must be exactly one. Non-unit scaling, native scale
compensation and the alternative non-quaternion rotation blend remain unsupported.
The implementation follows the native mathematical order with float/libm
arithmetic; it does not claim bit-identical native reciprocal/paired-single
approximations. Live comparison remains required.

## Separate hand models

`sst_model_linked_pose` evaluates a child model's bind hierarchy and then replaces
explicitly mapped joint worlds. The source and child use a common coordinate
space. Target IDs must be unique. Overrides are absolute and do not propagate
into descendants; this models a timing-C callback after world evaluation.
Only linked source joints are read. It produces the child's checked position
and normal palettes without changing the parent, child resources or native state.

Native player setup has six quaternion layers: even0/2/4 bind the waist subtree
plus center, and odd1/3/5 bind the backbone1 subtree. For the owned `pl_body`, these
are disjoint20/15-node sets covering all35 joints. The native hand callback copies
the body's handL/handR worlds into the separate hand model. Seven geometry shapes
per side also require selection; this API supplies transforms, not that geometry.

Both APIs check bounded spans and output aliases, including bindings, masks,
links, source matrices and borrowed resource buffers, before writes. Invalid
buffer extents/aliases reject without writes; admitted outputs clear on failure.
No allocation, file/network access, native object reads/writes or animation-cache
teaching occurs in the library.

## Validation

The current synthetic fixtures pass81,708 assertions under address,
undefined-behavior and leak sanitizers:8,985 pose and72,723 model assertions.
Analytical checks cover normalized two-layer rotations/translations, a separately
evaluated three-layer rotation, order dependence, all quaternion diagonal
branches, near-equal rotations, opposite hemispheres, missing tracks/masks,
post-blend defaults, non-unit rejection, six slots and linked-joint semantics.
The existing1,024 corrupted-span cases now also exercise both new APIs, including
failure clearing and immutable inputs. Local peak RSS was211.9MiB under the
existing512MiB cap and3GiB free reserve on both OSes, with one compiler.

Owned-resource C checks use the45-frame `waitS` and nine-frame `LiftS_UtoD`
resources with the35-node `pl_body`. There are57 masked cases (three idle phases
by19 carry phases) and25 controlled six-layer transition cases. Both `pl_hands`
and `al_hands` are tested across these82 body cases. Each hand model has three
joints and two visible matrices. The body produces30,176 matrices per palette
across the82 cases; each hand model produces164 matrices per palette.

The57 masked cases agree with separately selected local S/R/T followed by the
earlier FK/palette path within0.000031 per element. Linked joint/view matrices
match the selected body wrists exactly. Using the carry resource alone differs
from the masked result by up to3.94375 in a joint-matrix element. The25 transition
cases establish bounded finite poses and exact wrist links, not a separately
observed native transition. Weights are test inputs; actual runtime layer weights
have not been observed. All original input bytes remain unchanged.

The previous rest-palette comparison and18 expected non-unit lift-frame
rejections also pass with this source. Earlier model/pose receipts record their
own revisions. Raw resources, S/R/T samples and matrices remain private.

- [Current synthetic checks](../validation/resource-layers-20261115.json)
- [Owned masked-body/hand checks](../validation/resource-layers-owned-20261115.json)

Next: observe actual native layer state and callbacks, qualify the full body/hand
geometry manifest, compare inside the running game, then accept a cold observer
and reverse roles with independent full clients. No new gameplay acceptance or
complete object synchronization is claimed here.

## Primary references

Original C implementation; pinned references describe behavior and mathematics:

- [Native blend and partial binding, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/nw4r/g3d/g3d_anmchr.cpp)
- [Raw resource evaluation, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/nw4r/g3d/res/g3d_resanmchr.cpp)
- [Post-blend node defaults, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/nw4r/g3d/res/g3d_resnode.cpp)
- [Player masks and hand callback, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/d/d_player_mdl.cpp)
- [Quaternion conversion/slerp, ogwse5a4d36](https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/revolution/MTX/quat.c)
- [Quaternion matrix construction, tpc8fa8c9](https://github.com/zeldaret/tp/blob/c8fa8c9e2aab72cf4e5db0e5d1c84a9ea6ee6eb0/libs/dolphin/src/mtx/mtx.c)

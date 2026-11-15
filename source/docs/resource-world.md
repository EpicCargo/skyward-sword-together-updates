# Ordered joint-world operations

`sst_resource_world_ops` applies explicit operations to a caller-owned skeleton
between forward kinematics and `sst_mdl0_palettes`. It supplies the ordering needed
for post-FK limb reconstruction and absolute joint corrections. The decoder and
model resources remain immutable. The live renderer does not call this API yet.

Each operation identifies a target joint and a proper3x3 pre-rotation in the
common coordinate space. The linear result is `rotation * current_joint.linear`.
With an anchor, position becomes `current_anchor * local_offset + world_offset`.
`SST_WORLD_KEEP_POSITION` instead retains the current target position and adds
`world_offset`; its local offset must be zero. Offsets are finite, bounded values.

Operations run in order, so an anchor sees earlier changes. Self anchors and
repeated targets are valid. A rotation does not automatically move descendants.
In particular, rebuilding a limb is an explicit sequence: update the proximal
joint, then place each next joint from the updated parent and a supplied segment
vector. Other joints retain their input worlds. This models a post-FK callback;
it does not reproduce a per-node timing-B callback's descendant behavior by itself.

The API accepts1..256 joints,0..256 operations, and bounded output capacity up to256.
All buffer extents must be valid and disjoint, including input and operations.
Invalid extents or overlap reject before writes. Otherwise failure clears the
bounded output; no partial chain is published. Unused output rows clear on success.
Inputs, operation arrays and resources remain unchanged. All matrices must have
finite components in[-1e6,1e6]; rotations use the existing palette builder's proper
rigid tolerances. Intermediate results are also checked, including accumulated
scale/skew from repeated approximate rotations. No allocation, file/network I/O,
native object access, cache learning or automatic callback-state selection occurs.

Synthetic analytic checks cover world/local multiplication order, root position,
anchor and self-anchor ordering, repeated targets, no descendant propagation,
maximum-size chains,256 quarter-turns, approximate rotation drift, bounds, aliases,
nonfinite/reflected/scaled transforms, late failure and1,024 deterministic mutated
operations. All12,968 assertions pass with address, undefined-behavior and leak
sanitizers. Existing8,985 pose and72,723 model assertions pass too. See
[synthetic evidence](../validation/resource-world-20261115.json).

The actual API also replaces the private inline leg-chain prototype when comparing
against the prior native capture:480 records/32 sampled frames,16 idle and16 carry.
The new result agrees with that prototype within5.7220459e-6 per world-matrix element.
Carry position-palette max error remains0.765304089; normal linear max error remains
0.00790379569. The original selected-track recipe without the leg callback has
position max error2.98140335. Hand errors are unchanged. These are game coordinate
and matrix-element errors, not pixels. See
[owned-resource comparison](../validation/resource-world-owned-20261115.json).

The same API can attach a child model by adding a caller-supplied parent world
as an anchor and explicitly transforming each child's world. A separate hybrid
diagnostic evaluates owned `al_face`/`pl_head` models and default animations under
the **observed native body-head joint**, not a generated parent. Across32 captured
frames, face frame0 has max position errors0.00048828125 idle/0.000549316406 carry,
and max normal linear errors below2.4e-7. Head/hair frame0 still has errors up to
0.810333014 idle/0.29196167 carry, with normal errors0.0989589542/0.0335944742.
Scanning the12 head and3 face integer-frame candidates does not improve those
maxima. The native child animation frame was not observed; this scan is only a
diagnostic fit. Separate head/hair callbacks remain necessary to investigate.
These results support the default face palette/parent transform path under this
hybrid scope, not independent cold rendering or complete resource/geometry identity.
The scalar child diagnostic is included in the owned-resource evidence above.

This comparison uses explicit test inputs: one selected lower/upper track, unit
layer weights, and zero limb correction angles/offsets. Those values have NOT been
observed as native callback state. The segment lengths follow the
[pinned Wii callback reference](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/d/d_player_mdl.cpp#L1449)
and are consistent with the previous HD capture, but the remaining HD callback
state still needs qualification. No new game was launched for this comparison.
Raw models, animations, palettes and captures remain private. Skirt/head/gaze,
controller blending, separate face/head palettes and coherent geometry selection
remain unfinished. No generated pose is accepted for replacement rendering, and
cold carry/full object synchronization are not fixed by adding this API.

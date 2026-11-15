# Independent model palettes

`client/src/sst_resource_model.c` extends the resource pose evaluator with model
binding and weighted position/normal palettes. It has no renderer callers yet.
The cold observer's missing carried-pot display remains an integration task.

The library borrows immutable caller-owned bytes. It admits MDL0 version11,
ordered unit-scale bind hierarchies, no billboards and NORMAL/APPROX envelope modes0/1.
Names, IDs, parent links, matrix maps, stored bind/inverse-bind matrices and
complete NodeTree/NodeMix programs are checked before publishing the view.
Limits are16MiB per container,256 joints,1,024 matrix slots,4,096 operations per
program,65,536 program bytes and16 influences per blend. Unsupported scale,
billboard and envelope modes fail explicitly.

NodeTree validates NODEDESC and MTXDUP dependencies. NodeMix prepares separate
EVPMTX envelope matrices with inverse bind transforms, then evaluates NODEMIX
in authored influence order. Weights must be finite, nonnegative and sum to one
within1e-5; admitted weights are never normalized. Every final matrix slot must
agree with the model's direct-node/mixed matrix map.

For NORMAL/APPROX envelopes and the supported unit-scale root, native view construction
copies the weighted linear3x3 into the normal palette. Applying inverse transpose
instead would change lighting. Position and normal outputs are separate bounded
arrays; normal padding is zero. Buffer overlap, including overlap with borrowed
resources, is rejected before writes. Admitted outputs are cleared on failure.

Native player model loading changes the body's envelope mode to APPROX. For the
admitted unit root, NORMAL and APPROX share this normal rule; the synthetic
fixture checks identical results in both modes. The library never changes the
borrowed mode byte. EXACT mode2 and non-unit scaling remain unsupported.

`sst_model_bind` matches model nodes and CHR0 tracks by name; absent tracks use
model bind S/R/T. `sst_model_pose` samples one animation, composes joint worlds,
and produces both palettes. It does not implement the native primary/secondary
blend stack, callbacks, gaze, hand geometry or attachment selection.

## Rotation precision

Forward kinematics now uses the native256-step FIdx sine/cosine convention,
including float degree-to-index conversion, magnitude/sign handling, linear
interpolation and independently rounded sample deltas. The mathematical samples
are generated on demand; no native table or game data is embedded. Standard
trigonometry creates small joint errors that inverse-bind translations amplify.
This correction applies to the shared unit hierarchy evaluator. Its original
CHR0 curve interpolation and scale restrictions are unchanged.

Synthetic regression cases use a half-segment rotation and a long child offset
to distinguish the native convention from exact sine/cosine. Positive, negative,
quarter-turn and multiple-revolution cases exercise signs and index reduction.
The model fixture uses analytical two-bone skinning and NORMAL-normal expectations,
missing-track bind fallback, authored weight preservation, malformed programs,
alias protection, truncated prefixes and1,024 deterministic corrupted spans.

## Validation

The exact C implementation passes50,936 assertions under address,
undefined-behavior and leak sanitizers:8,985 pose checks and41,951 model checks.
Each fixture includes1,024 deterministic corrupted spans. Guarded local peak RSS
was195.3MiB; one compiler ran under the existing512MiB cap and3GiB reserve on both
Windows and Linux. Source CI explicitly executes both fixtures.

Private owned-resource C checks admit the35-node `pl_body` model,384 world slots
and368 visible matrices. The nine-frame steady carry resource produces6,992
position and6,992 normal matrices at19 half-frame positions, including4.5. The
ten-frame lift resource admits three sampled frames (1,104 matrices per palette)
and rejects18 non-unit-scale frames with cleared outputs. These rejections are
expected restrictions, not complete lift-animation support.

An independent private bind oracle uses authored forward bind matrices for direct
slots and identity for weighted inverse-bind slots. Stored and reconstructed rest
palettes both match it within0.0000148 per element, below the unchanged0.002 gate.
The reconstructed path previously failed at0.0165 with standard trigonometry.
Owned-resource C checks passed sanitizers at191.9MiB peak; all input bytes remained
unchanged. Animated checks establish finite complete palettes and the normal
rule, not comparison with a genuine native animated pose. Only this body model
and these two animation resources are qualified by this C audit.

- [Synthetic C checks](../validation/resource-model-20261115.json)
- [Owned model/resource checks](../validation/resource-model-owned-20261115.json)

No live native pose or new gameplay acceptance is claimed. Raw model/animation
bytes, bind records and palettes remain private. The module has no renderer
callers, and cold carry display is still unresolved.

## Remaining renderer work

Native player setup uses six quaternion-blended body layers. Even layers bind
the waist subtree plus the center node; odd layers bind the backbone1 subtree.
The hand callback copies the body's left/right hand world matrices into the
separate hand model. Hand geometry also selects among seven shapes per side.
These are requirements for the integration, not behavior supplied by this module.

Qualify all body parts and the carrying hand geometry; reproduce the relevant
native animation layers and callbacks. Compare generated palettes with a genuine
native carried pose before enabling them for a cold observer. The existing local
animation cache must only learn from genuine local state. Full object classes,
throws/breaking, contention, transitions and independent-client acceptance remain
separate co-op work.

## Primary references

The C implementation is original. Format and behavior references are pinned:

- [MDL0 parsing and bytecode, noclip.website8f891b8](https://github.com/magcius/noclip.website/blob/8f891b855d862958c82b986dd90e0d5ec760bd92/src/rres/brres.ts)
- [Native world/skinning calculation, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/nw4r/g3d/g3d_calcworld.cpp)
- [Native view/normal calculation, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/nw4r/g3d/g3d_calcview.cpp)
- [Native model-default rotation, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/nw4r/g3d/res/g3d_resnode.cpp)
- [Player model setup, animation masks and hand callback, Skyward Sword96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/d/d_player_mdl.cpp)
- [NintendoWare FIdx convention, ogwse5a4d36](https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/math/math_triangular.cpp)
- [NintendoWare Euler composition, ogwse5a4d36](https://github.com/doldecomp/ogws/blob/e5a4d369011062fe243aa63f6c2e2853854c395d/src/nw4r/math/math_types.cpp)

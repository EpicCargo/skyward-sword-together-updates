# Independent resource pose evaluator

`client/src/sst_resource_pose.c` is the first implementation step toward the
cold-observer carry fix. It evaluates local animation resources without reading
guest objects, changing the player, learning an animation cache entry or sending
skeletal data over the network. It is linked as a library; the renderer does not
call it yet. A fresh client's missing carried-pot display remains unresolved.

## Implemented scope

The caller supplies an immutable, bounded resource container and its CHR0 chunk
offset. Opening checks versions3–5, chunk/dictionary/node ranges, names and curve
payloads before publishing a borrowed view. Shared strings may live outside the
chunk, while curve payloads stay inside it. The evaluator supports constant
channels, packed32/48/96-bit Hermite keys and8/16/32-bit sampled channels. It
preserves resource scaling flags and resolves explicit model-default channels
from the caller's bind pose.

Sampled channels interpolate their encoded values directly, including rotations.
They do not substitute a shortest-angle path. Coincident key times are valid:
the initial endpoint selects the first key, while an interior coincident time
selects the last key before the next strictly later key. This avoids dividing by
a zero interval and matches the native key-selection behavior. A real carry
resource exposed this case after the initial synthetic checks passed.

Frames must be finite and within the resource duration; the caller owns loop
timing. The view is borrowed and its bytes must remain alive and immutable.
Limits are16MiB per container,256 joints,4,096 keys per channel and127-byte joint
names. Malformed outputs are cleared. There are no allocations or file/network
operations inside the library.

`sst_resource_pose_world` composes row-major3x4 model-space joint matrices from
resolved local S/R/T and an ordered parent hierarchy. This implementation admits
unit scale only. It rejects cycles, forward parents, invalid values, non-unit
scale and results outside the bounded range, clearing the output. Native scale
compensation, skin weights, inverse bind transforms and per-draw palettes are
not implemented by this function.

## Validation

The synthetic fixture passes8,967 assertions under address, undefined-behavior
and leak sanitizers. It covers all seven channel formats, fractional-frame
numeric expectations, repeated times and initial endpoints, model defaults,
versions, hierarchy rotations/translations, malformed tables, every truncated
prefix through the required string terminator, and1,024 deterministic corrupted
spans. It also checks immutable input bytes and cleared failure outputs. The
standard client test target and source CI explicitly run the fixture.

Private checks use the user's owned resources, with no game launch. The two
small-carry animations provide1,480 local joint/frame samples. The steady
nine-frame resource provides665 unit-scale body matrices across35 matched joints
and19 half-frame positions, including the observed4.5 steady phase. This model
binding checks `pl_body`; other outfits are not covered. Composed body
rest matrices differ from the model's stored bind matrices by at most0.000624 per
matrix element. The separate ten-frame lift resource provides105 admitted body
matrices;18 of its21 sampled frames have non-unit scale and are explicitly
rejected by the restricted hierarchy evaluator. These rejections are expected
limits, not rendering success. Only hashes/counts are published:

- [Synthetic checks](../validation/resource-pose-20261115.json)
- [Owned-resource checks and limits](../validation/resource-pose-owned-20261115.json)

No live native carried-pose comparison or new gameplay acceptance is claimed.
Raw model/animation files, bind references and skeletal matrices remain private.

## Renderer integration still required

Bind this evaluator to verified model resources and the complete body-part
manifest. Reproduce native primary/secondary animation blending and any relevant
model callbacks, then construct the weighted position and normal palettes for
each draw. Body joint matrices alone are insufficient for the existing native
skinning buffers. Carrying hand variants and attachments also need a coherent
independent geometry/pose source. Compare the result with genuine native carry
before allowing a cold observer to use it. Forced viewer draws must not teach
the ordinary local cache or change native held-object linkage.

## Format references

The implementation is original C. Public primary implementations supplied format
and interpolation references; no game assets or upstream implementation source
are embedded in it:

- [NintendoWare CHR0 evaluation in the Skyward Sword decompilation, pinned96f2dd5](https://github.com/zeldaret/ss/blob/96f2dd5497c4ceb595de76fd897fbb1542b27323/src/nw4r/g3d/res/g3d_resanmchr.cpp)
- [BRRES model/animation parsing in noclip.website, pinned8f891b8](https://github.com/magcius/noclip.website/blob/8f891b855d862958c82b986dd90e0d5ec760bd92/src/rres/brres.ts)

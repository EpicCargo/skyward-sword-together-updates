// SPDX-License-Identifier: MIT
// Ordered operations on caller-owned joint worlds; no native state or I/O.
#pragma once
#include "sst_resource_pose.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SST_WORLD_KEEP_POSITION UINT16_MAX
#define SST_WORLD_MAX_OPS 256u
typedef struct {
    uint16_t joint,anchor;
    float rotation[9]; // row-major proper pre-rotation in the common world space
    float local_offset[3],world_offset[3];
} SstResourceWorldOp;
// Copy input worlds, then execute operations in order. Linear result is
// rotation * current_joint.linear. Position is current_anchor * local_offset
// + world_offset, or the current joint position + world_offset when anchor is
// KEEP_POSITION (local_offset must then be zero). Anchors read earlier updates;
// self anchors and repeated targets are allowed. No descendant propagation.
// This supports explicit post-FK callbacks/limb chains without inferring native
// correction angles, offsets, blend weights, actions or model-specific lengths.
// Inputs/rotations must be bounded proper rigid matrices; approximate unit
// bases use the same .005 Gram/.01 determinant tolerances as model palettes.
// Count/capacity1..256, count<=capacity; operations0..256. All buffers must be
// disjoint, including input and operations. Invalid extents/overlap reject
// before writes; otherwise failure clears the bounded output. Success clears
// unused output rows. Inputs remain immutable; no allocation or cache teaching.
bool sst_resource_world_ops(const float (*input)[12],unsigned count,
                            const SstResourceWorldOp *ops,unsigned op_count,
                            float (*out)[12],unsigned capacity);
#ifdef __cplusplus
}
#endif

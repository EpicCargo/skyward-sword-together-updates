// SPDX-License-Identifier: MIT
// Read-only CHR0 evaluation from an owned immutable resource buffer.
// No file/network/guest access, allocations, model mutation or cache teaching.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SST_CHR0_MAX_BYTES (16u*1024u*1024u)
#define SST_CHR0_MAX_JOINTS 256u
#define SST_CHR0_MAX_KEYS 4096u
#define SST_CHR0_NAME_BYTES 128u
typedef struct {
    float scale[3],rotation[3],translation[3]; // native resource degrees, not radians
    uint32_t flags; // retain native scaling/compensation flags for the future model evaluator
} SstResourceSrt;
// Opaque borrowed view: initialize with open, do not edit. Caller retains the
// complete immutable container, including its shared string pool, until done.
typedef struct {
    const uint8_t *data;size_t bytes,offset,end,dictionary;
    uint16_t duration,joints;uint32_t loop_mode,scaling_rule;
} SstChr0;
// Supported CHR0 versions3..5. All node names, tables and tracks are checked
// before publishing the view. Shared strings may lie outside the CHR0 chunk;
// joint/track payloads must stay inside it. Failed outputs are cleared.
bool sst_chr0_open(const void *container,size_t bytes,size_t offset,SstChr0 *out);
bool sst_chr0_joint_name(const SstChr0 *view,unsigned joint,char *out,size_t capacity);
// Frame must be finite and inside[0,duration]; caller owns loop timing.
// bind supplies model defaults for USE_MODEL flags. NULL is accepted only when
// no component needs model defaults. CV tracks use native ordinary linear
// interpolation, including rotations; no shortest-angle substitution.
// Output is local S/R/T ONLY, not a skinning palette or combined native pose.
bool sst_chr0_sample(const SstChr0 *view,unsigned joint,float frame,
                     const SstResourceSrt *bind,SstResourceSrt *out);
// Model-space forward kinematics for a unit-scale hierarchy/forest. Parents
// must be-1 or a preceding joint. This restricted form deliberately rejects
// non-unit scale: native segment compensation is not implemented here.
// Row-major3x4 matrices, Rz*Ry*Rx local rotation, no skin weights or bind inverse.
// Uses the native interpolated FIdx rotation convention (generated math samples).
// Output capacity1..256 matrices; failure clears that bounded output.
bool sst_resource_pose_world(const int16_t *parents,const SstResourceSrt *local,
                             unsigned count,float (*out)[12],unsigned capacity);
#ifdef __cplusplus
}
#endif

// SPDX-License-Identifier: MIT
// Borrowed immutable MDL0 resources. No guest reads, file I/O or native writes.
#pragma once
#include "sst_resource_pose.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SST_MDL0_MAX_MATRICES 1024u
#define SST_MDL0_MAX_PROGRAM_BYTES 65536u
#define SST_MDL0_MAX_OPS 4096u
#define SST_MDL0_MAX_INFLUENCES 16u
typedef struct {
    const uint8_t *data;size_t bytes,offset,end,bones,matrix_table,tree,tree_end,mix,mix_end;
    uint16_t nodes,world_matrices,view_matrices;uint32_t scaling_rule;uint8_t envelope_mode;
    char name[SST_CHR0_NAME_BYTES];
} SstMdl0; // opaque, initialize with open; retain immutable input until done
typedef struct {
    char name[SST_CHR0_NAME_BYTES];int16_t parent;uint16_t matrix;
    SstResourceSrt bind;float forward_bind[12],inverse_bind[12];
} SstModelNode;
typedef struct {
    SstMdl0 model;SstChr0 animation;int16_t tracks[SST_CHR0_MAX_JOINTS];uint16_t matched;
} SstModelBinding; // opaque by-value views; both underlying buffers remain borrowed
// Scoped to version11, unit-scale bind hierarchy, no billboards, NORMAL or APPROX
// envelope modes. They share the linear normal rule for the admitted unit root.
// Checks names, bind matrices, complete NodeTree/NodeMix programs and weights.
bool sst_mdl0_open(const void *data,size_t bytes,size_t offset,SstMdl0 *out);
bool sst_mdl0_node(const SstMdl0 *model,unsigned index,SstModelNode *out);
// Input joint worlds must be rigid, model-space and in resource node order.
// NORMAL/APPROX envelope normals copy the linear weighted matrix, with zero padding.
// Authored weights are checked but never normalized or reordered.
// Valid disjoint bounded outputs are cleared on failure. Invalid buffer extents or
// overlap (including borrowed resource bytes) reject before any output write.
bool sst_mdl0_palettes(const SstMdl0 *model,const float (*joints)[12],unsigned count,
                       float (*positions)[12],float (*normals)[12],unsigned capacity);
// A missing animation channel uses that model node's bind S/R/T. This is one
// animation plus model defaults, NOT the native primary/secondary blend stack.
bool sst_model_bind(const SstMdl0 *model,const SstChr0 *animation,SstModelBinding *out);
bool sst_model_pose(const SstModelBinding *binding,float frame,float (*joints)[12],unsigned joint_capacity,
                    float (*positions)[12],float (*normals)[12],unsigned palette_capacity);
#define SST_MODEL_MAX_LAYERS 6u
typedef struct {
    const SstModelBinding *binding;const uint8_t *mask;unsigned mask_count;
    float frame,weight;
} SstModelLayer;
// Up to six ordered layers of the same model. NULL mask/count0 selects all
// joints; otherwise exactly model.nodes binary bytes. Missing tracks do not
// contribute weight. Zero active contributors use bind; one preserves native
// S/R/T directly; multiple contributors use ordered quaternion slerp and
// per-joint normalized translation. Model-default channels are patched AFTER
// blending, only when requested by every contributing channel. Unit scale only.
// No native layer weights, loop timing, callbacks or geometry selection inferred.
// Weights must be finite/nonnegative and<=1e6. Frames are checked for nonzero
// layers. NULL binding denotes an empty zero-weight layer without a mask.
bool sst_model_layers(const SstMdl0 *model,const SstModelLayer *layers,unsigned layer_count,
                      float (*joints)[12],unsigned joint_capacity,float (*positions)[12],
                      float (*normals)[12],unsigned palette_capacity);
typedef struct {uint16_t target,source;} SstModelJointLink;
// Absolute world-joint overrides after child bind FK, matching a timing-C
// callback. Source and child must use the same coordinate space. Unmapped
// joints keep their bind worlds; overrides do NOT propagate into descendants.
// Target IDs must be unique. Only explicitly linked source joints are read.
// Does not choose the seven native hand shapes or modify any native object.
bool sst_model_linked_pose(const SstMdl0 *model,const float (*source)[12],unsigned source_count,
                           const SstModelJointLink *links,unsigned link_count,float (*joints)[12],
                           unsigned joint_capacity,float (*positions)[12],float (*normals)[12],unsigned palette_capacity);
#ifdef __cplusplus
}
#endif

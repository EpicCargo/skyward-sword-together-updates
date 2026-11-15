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
    uint16_t nodes,world_matrices,view_matrices;uint32_t scaling_rule;
    char name[SST_CHR0_NAME_BYTES];
} SstMdl0; // opaque, initialize with open; retain immutable input until done
typedef struct {
    char name[SST_CHR0_NAME_BYTES];int16_t parent;uint16_t matrix;
    SstResourceSrt bind;float forward_bind[12],inverse_bind[12];
} SstModelNode;
typedef struct {
    SstMdl0 model;SstChr0 animation;int16_t tracks[SST_CHR0_MAX_JOINTS];uint16_t matched;
} SstModelBinding; // opaque by-value views; both underlying buffers remain borrowed
// Scoped to version11, unit-scale bind hierarchy, no billboards, NORMAL envelope
// mode. Checks names, bind matrices, complete NodeTree/NodeMix programs and weights.
bool sst_mdl0_open(const void *data,size_t bytes,size_t offset,SstMdl0 *out);
bool sst_mdl0_node(const SstMdl0 *model,unsigned index,SstModelNode *out);
// Input joint worlds must be rigid, model-space and in resource node order.
// NORMAL envelope normals copy the linear weighted matrix, with zero padding.
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
#ifdef __cplusplus
}
#endif

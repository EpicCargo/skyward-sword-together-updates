// SPDX-License-Identifier: MIT
// Adapted from the user-provided SST handoff dated2026-10-30; original archive source retained.
// Development-only census of the meshes drawn for the local Link (SST_MESH_CENSUS=1): which hand and
// grip variants exist, which are recognised, and whether an unrecognised model-only piece sits on a
// verified wrist joint. It only observes and logs (bounded); it never adds or folds a mesh mapping.
#pragma once
#include "sst_net.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SST_CENSUS_ENTRIES 256
#define SST_CENSUS_LINES 4000
typedef struct {
    uint64_t hash;                      // sst_avatar_signature of the vertex buffer
    uint32_t size, positions, normals;  // vertex buffer size; palette sizes (0, 0: model-only / rigid)
    uint32_t count, first;              // draw range: one buffer can hold several submeshes
    uint16_t known;                     // sst_avatar_mesh_id (0: unrecognised)
} SstCensusDraw;
typedef struct {
    unsigned entries, unknown_entries, unknown_rigid_near, unknown_on_wrist;
    uint64_t unknown_hand_presents; // presents with an unrecognised hand-sized model-only draw near Link
    uint64_t presents, lines, suppressed;
} SstCensusStats;

bool sst_census_enabled(void); // SST_MESH_CENSUS=1, read once
// One Link-signature draw in the main pass. local/view as for sst_avatar_local_skin; model = the draw's model matrix.
void sst_census_draw(const SstCensusDraw *draw, const SstPose *local, const float view[12], const float model[12],
                     const void *positions, uint64_t present);
SstCensusStats sst_census_stats(void);
void sst_census_reset(FILE *output); // tests: clear state and send lines to output (NULL: stderr)

#ifdef __cplusplus
}
#endif

// Validated, bounded joint-local offsets for the observed starting-outfit hands.
#pragma once
#include "sst_net.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint16_t body, joint;
    uint32_t positions, normals;
    float local[12];
} SstHandOffset;
typedef struct { size_t entries, ready, blocked, blocked_parents; uint64_t placements; } SstHandStats;
void sst_hand_reset(void);
void sst_hand_forget_clip(uint16_t clip);
void sst_hand_observe(uint16_t mesh, const SstPose *pose, uint16_t body,
                      size_t positions, size_t normals, const float *joint, const float *model);
bool sst_hand_lookup(uint16_t mesh, const SstPose *pose, bool loop, float length, SstHandOffset *out);
// Compose only finite, nonsingular, bounded joint-local offsets. Never writes on failure.
bool sst_hand_compose(const float *joint, const float *local, float *out);
void sst_hand_note_placement(void);
SstHandStats sst_hand_stats(void);
#ifdef __cplusplus
}
#endif

// Render producer only: diagnostic hash excludes LRU/placement counters.
uint64_t sst_hand_teaching_hash(void);

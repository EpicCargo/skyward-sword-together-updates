// SPDX-License-Identifier: MIT
// Opt-in in-process diagnostics. Raw records belong only in private test folders.
#pragma once
#include "sst_animation.h"
#ifdef __cplusplus
extern "C" {
#endif
// Version2 optional payload: observed signed-angle units, and native x/y/z/w
// quaternion components. No control weights, pointers or inferred values.
typedef struct {
    int16_t waist[2],head[3],torso[3],hair[10],hair_yaw,actor_yaw;
   float hair_quat[4][4];
} SstProbeControls;
typedef struct SstProbeTracks {
    SstScene scene;uint8_t primary,valid_mask;uint16_t ids[6];float frames[6],lengths[6];bool controls_valid;SstProbeControls controls;
} SstProbeTracks; // observed tracks/optional corrections; NO inferred/native blend weights
typedef bool (*SstProbeTrackReader)(const SstPose *expected,SstProbeTracks *out,void *user);
// On-disk little-endian version1. All reserved bytes zero. Matrices are float32
// in canonical actor-origin/yaw space. Records contain NO native pointers.
typedef struct {
    uint32_t bytes,position_bytes,normal_bytes;uint16_t mesh,context;uint64_t frame;
    uint32_t tick;uint16_t clip,action;float phase;uint8_t equipment,primary,valid_tracks,reserved;
    uint16_t ids[6];float frames[6],lengths[6];char stage[9];uint8_t room,layer,entrance;
    float model[12];
} SstProbeRecord;
#define SST_PROBE_MAX_BYTES (32u*1024u*1024u)
#define SST_PROBE_MAX_CONTEXT_SAMPLES 16u
// Requires LARECOMP_DEBUG_DATA=1, SST_POSE_PROBE=1 and a nonempty absolute
// SST_POSE_PROBE_DIR. No file operations when disabled. Creates native-poses.bin
// exclusively, never overwrites. SST_POSE_CONTROLS=1 instead creates a
// version2 native-poses-controls.bin with an additional104-byte control payload
// after each160-byte header, before matrices; weights remain unobserved.
// At most16 frames/context,64 meshes/frame,
//32MiB total. Only clip26 idle/action0 or steady-small-carry/action67 is recorded.
bool sst_pose_probe_start(SstProbeTrackReader reader,void *user);
void sst_pose_probe_reset(void); // render thread, after observation has stopped
#ifdef __cplusplus
}
#endif

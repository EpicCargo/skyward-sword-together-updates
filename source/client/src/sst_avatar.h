// SPDX-License-Identifier: MIT
// Adapted from the user-provided SST handoff dated2026-10-30; original archive source retained.
// First world-avatar renderer: replay verified Link meshes using each peer's world transform.
// Uses the local outfit/equipment; observed locomotion can replay through sst_animation.h.
#pragma once
#include "sst_net.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool sst_avatar_mesh(const void *vertices, size_t size);
uint16_t sst_avatar_mesh_id(const void *vertices, size_t size); // zero: unverified geometry
// FNV-1a of the first 256 bytes: the geometry signature sst_avatar_mesh_id compares (zero below 256 bytes).
uint64_t sst_avatar_signature(const void *vertices, size_t size);
// Pose identity for already verified geometry. Rigid hand variants with a proved common
// wrist node share observed poses; this does not authorize geometry or change its shape.
uint16_t sst_avatar_pose_mesh(uint16_t mesh, size_t position_size, size_t normal_size);
// Census-evidenced parent joint of a model-only hand pose mesh: its origin keeps a constant offset from
// this joint of a skinned palette of exactly `positions` bytes while its rotation varies (child offset).
// Evidence only; sst_animation still validates every observation live and blocks on disagreement.
bool sst_avatar_hand_parent(uint16_t pose_mesh, uint16_t *joint, uint32_t *positions);
// Recognized geometry also appears in other passes. Its model must be near native Link in
// the verified gameplay camera before capturing or replaying it as a world character.
bool sst_avatar_local_draw(const SstPose *local, const float view[12], const float model[12]);
// Skinned draws can have a model origin away from Link; verify their actual bone origins too.
bool sst_avatar_local_skin(const SstPose *local, const float view[12], const float model[12],
                           const void *positions, size_t position_size);
// Visible equipment in SstPose.equip_flags (bits 0-1; 5-7 are world/motion flags). A remote's sword is drawn only when
// its own pose reports one, in its own drawn/sheathed state, independently of the observer's equipment.
#define SST_EQUIP_SWORD 0x01
#define SST_EQUIP_SWORD_DRAWN 0x02
bool sst_avatar_equipment(uint16_t mesh);                       // sword equipment pieces (55-57)
uint16_t sst_avatar_equipment_pose(uint16_t mesh, bool drawn);  // pose identity per drawn/sheathed state
void sst_avatar_note_local(uint16_t mesh, uint64_t present);    // each recognised local Link draw
uint8_t sst_avatar_local_equipment(void);                       // SST_EQUIP_* of the last finished present
void sst_avatar_equipment_reset(void); // render-thread cache reset / stopped renderer reboot
// Pose identity to replay a recognised local draw for a remote (0: do not draw it for that remote).
uint16_t sst_avatar_remote_pose_mesh(uint16_t mesh, uint16_t pose_mesh, uint8_t remote_flags);
// World yaw/translation converted into a transform of existing view-space bone matrices.
bool sst_avatar_transform(const SstPose *local, const SstPose *remote, const float view[12], float out[12]);
void sst_avatar_matrix(float matrix[12], const float transform[12], bool normal);
#ifdef __cplusplus
}
#endif

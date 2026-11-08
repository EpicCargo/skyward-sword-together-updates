// Pure peer-position teleport policy. The caller serializes access (game thread).
// This module never reads guest memory, requests snapshots, applies progress or
// performs a load. UI requests must reach this policy through a separate mailbox.
#pragma once
#include "sst_net.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SST_TELEPORT_POSE_MAX_AGE_MS 1000u
#define SST_TELEPORT_WAIT_TIMEOUT_MS 15000u
#define SST_TELEPORT_LOAD_TIMEOUT_MS 45000u

typedef enum {
    SST_TELEPORT_IDLE, SST_TELEPORT_WAIT, SST_TELEPORT_LOAD_AREA,
    SST_TELEPORT_PLACE, SST_TELEPORT_CANCEL
} SstTeleportAction;
typedef enum {
    SST_TELEPORT_REASON_NONE, SST_TELEPORT_REASON_WAIT_LOCAL,
    SST_TELEPORT_REASON_WAIT_POSE, SST_TELEPORT_REASON_WAIT_PLAYER,
    SST_TELEPORT_REASON_WAIT_LOAD, SST_TELEPORT_REASON_LEFT,
    SST_TELEPORT_REASON_REJOINED, SST_TELEPORT_REASON_CONNECTION,
    SST_TELEPORT_REASON_TARGET_AREA, SST_TELEPORT_REASON_TARGET_INSTANCE,
    SST_TELEPORT_REASON_LOCAL_INSTANCE, SST_TELEPORT_REASON_DEAD,
    SST_TELEPORT_REASON_INVALID, SST_TELEPORT_REASON_BOSS,
    SST_TELEPORT_REASON_TIMEOUT, SST_TELEPORT_REASON_CANCELLED,
    SST_TELEPORT_REASON_DONE
} SstTeleportReason;

typedef struct {
    bool active, loading, arrived;
    uint16_t peer_id;
    uint32_t nonce, connection_generation, presence_generation, peer_scene_generation;
    uint32_t before_load_instance, arrived_instance;
    uint64_t began_ms, load_started_ms, arrived_ms, last_step_ms;
    SstScene target;
    SstTeleportAction last_action;
    SstTeleportReason reason;
} SstTeleport;
typedef struct {
    SstTeleportAction action;
    SstTeleportReason reason;
    uint32_t nonce;
    SstScene scene; // LOAD_AREA only: validate against the native catalogue first
    SstPose pose;   // PLACE only: use ONLY pos/rot; never write health/action/equipment
} SstTeleportDecision;

// The caller also checks ONLINE, a non-self player ID, native executable/type,
// catalogue membership, menu/event/loader safety and local actor identity.
// Cross-area peers need a known SCENE, not an old world/progress snapshot.
bool sst_teleport_begin(SstTeleport *state, const SstPlayer *peer,
                        uint32_t connection_generation, uint64_t now_ms);
SstTeleportDecision sst_teleport_step(SstTeleport *state, const SstPlayer *peer,
    uint32_t connection_generation, const SstScene *local_scene, bool local_ready,
    uint32_t local_instance, uint64_t now_ms);
// Call synchronously only after the native load accepted the last LOAD_AREA.
// A failed/busy load remains retryable and must not acknowledge here.
bool sst_teleport_load_accepted(SstTeleport *state, uint32_t nonce, uint32_t old_local_instance);
// Call only after the native setter accepted the last PLACE. A failed setter may
// retry until deadline, or the caller can explicitly cancel with a reason.
bool sst_teleport_complete(SstTeleport *state, uint32_t nonce);
void sst_teleport_cancel(SstTeleport *state, SstTeleportReason reason);
void sst_teleport_reset(SstTeleport *state);
const char *sst_teleport_reason_text(SstTeleportReason reason);
#ifdef __cplusplus
}
#endif

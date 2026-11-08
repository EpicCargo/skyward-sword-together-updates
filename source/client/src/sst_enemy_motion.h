// Regular enemy movement reconciliation. Native AI/animation remain in the local game.
#pragma once
#include "sst_world.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SST_MOTION_CAPABLE 0x20
#define SST_MOTION_FRESH_MS 1500
#define SST_MOTION_TIMEOUT_MS 500
#define SST_MOTION_FUTURE_MS 50 // tolerate a network stamp just newer than this native-frame clock
// Follower smoothing (session 008). The sender emits about every 133 ms (100 ms gate on a
// 33 ms tick). The interpolation window follows the measured inter-arrival time, clamped.
#define SST_MOTION_INTERVAL_DEFAULT_MS 133
#define SST_MOTION_INTERP_MIN_MS 80
#define SST_MOTION_INTERP_MAX_MS 200
#define SST_MOTION_EXTRAP_MS 150    // bounded dead reckoning after the window, then hold
#define SST_MOTION_HANDOFF_MS 400   // previous owner may finish its in-flight tracks
#define SST_MOTION_SNAP_DISTANCE 1000.0f // larger jumps are teleports, not motion
bool sst_motion_valid(const SstEnemyMotion *m);
// Shared ownership rule (identical on host and clients): the lowest player id in the same team
// and exact stage/room/layer whose pose is fresh (<= 1500 ms), alive and motion-capable
// (pose flags & 0xe0 == 0x20: not boss, not event/menu, motion enabled).
bool sst_motion_eligible(uint8_t pose_flags, uint16_t health, uint64_t pose_ms, uint64_t now);
typedef struct {
    uint16_t id;
    const char *team;
    const SstScene *scene;
    uint64_t pose_ms;   // local receive time of the candidate's newest pose
    uint8_t pose_flags; // SstPose.equip_flags; only bits 5-7 matter here
    uint16_t health;
} SstMotionCandidate;
uint16_t sst_motion_pick_owner(const SstMotionCandidate *c, int count, const char *team,
                               const SstScene *area, uint64_t now);
// Lowest eligible ID in this area. Players in other teams, cutscenes, bosses or with movement off
// cannot own the stream. A stale owner is replaced without moving anyone's player character.
uint16_t sst_motion_owner(uint16_t mine, bool eligible, const SstScene *area, const char *team,
                          const SstPlayer *players, int count, uint64_t now);
typedef struct {
    bool used, has_target, has_local, has_rate;
    uint64_t key;
    uint64_t base_ms;    // local time the current interpolation segment started
    uint64_t received_ms; // network receive time of the newest sample (500 ms timeout clock)
    uint32_t instance, sequence;
    uint32_t presence_generation;
    uint32_t interval_ms; // smoothed inter-arrival, clamped to the interpolation range
    uint16_t profile, from;
    float start[3], target[3], velocity[3];
    float rate[3];       // measured units per ms between the last two samples (extrapolation)
    float local[3];      // native transform observed while no remote sample was live
    int16_t start_rot[3], target_rot[3], local_rot[3];
} SstMotionTrack;
typedef struct {
    SstScene scene;
    uint32_t load;
    uint16_t owner, previous_owner;
    uint64_t owner_since_ms;
    unsigned owner_changes, accepted, rejected; // diagnostics
    SstMotionTrack actors[SST_ENEMIES];
} SstMotionState;
// Tracks survive an owner change (handoff): only a new load or area wipes them.
void sst_motion_begin(SstMotionState *s, const SstScene *scene, uint32_t load, uint16_t owner);
void sst_motion_begin_at(SstMotionState *s, const SstScene *scene, uint32_t load, uint16_t owner, uint64_t now);
void sst_motion_observe(SstMotionState *s, uint64_t key, uint32_t instance, uint16_t profile);
// Also records the native transform, so a follower's first remote sample blends from it.
void sst_motion_observe_at(SstMotionState *s, uint64_t key, uint32_t instance, uint16_t profile,
                           const float pos[3], const int16_t rot[3]);
bool sst_motion_receive(SstMotionState *s, const SstEnemyMotion *m, uint64_t now);
bool sst_motion_sample(const SstMotionState *s, uint64_t key, uint32_t instance, uint64_t now,
                        float pos[3], int16_t rot[3], float velocity[3]);
void sst_motion_configure(bool enabled);
bool sst_motion_enabled(void);
#ifdef __cplusplus
}
#endif

// Shared world policy and enemy reconciliation. No game pointers or engine dependencies here.
#pragma once
#include "sst_net.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { SST_WORLD_STORY, SST_WORLD_ITEM, SST_WORLD_SCENE, SST_WORLD_DUNGEON,
       SST_WORLD_KINDS, SST_WORLD_BITS = 26 * 128, SST_WORLD_BYTES = SST_WORLD_BITS / 8 };
bool sst_world_flag_allowed(unsigned kind, unsigned bit);
bool sst_world_song(unsigned item);
bool sst_world_boolean_definition(uint8_t word, uint8_t shift_width, unsigned words);
bool sst_world_boss_profile(uint16_t profile);
bool sst_world_boss_actor(uint16_t profile, uint8_t actor_room, uint8_t current_room);
bool sst_world_boss_stage(const char *stage);
bool sst_world_trial_stage(const char *stage);
bool sst_world_enemy_profile(uint16_t profile);
bool sst_world_same_area(const SstScene *a, const SstScene *b);
typedef enum {
    SST_CATCHUP_READY, SST_CATCHUP_UNAVAILABLE, SST_CATCHUP_OTHER_TEAM,
    SST_CATCHUP_NO_AREA, SST_CATCHUP_BOSS, SST_CATCHUP_STALE,
    SST_CATCHUP_DEFEATED, SST_CATCHUP_INVALID_HEALTH, SST_CATCHUP_MENU, SST_CATCHUP_BUSY, SST_CATCHUP_TRIAL
} SstCatchupStatus;
SstCatchupStatus sst_world_catchup_status(const SstPlayer *peer, const char *team, uint64_t now_ms);
const char *sst_world_catchup_text(SstCatchupStatus status);
// Recovery destinations must be fresh, living teammates in ordinary gameplay.
bool sst_world_catchup_allowed(const SstPlayer *peer, const char *team, uint64_t now_ms);
// The button may request fresh state even if the displayed pose from another area is old.
bool sst_world_catchup_request_allowed(const SstPlayer *peer, const char *team);
uint64_t sst_world_enemy_key(uint16_t profile, uint16_t object_id, uint32_t params);

typedef struct {
    uint8_t known[SST_WORLD_KINDS][SST_WORLD_BYTES];
    uint8_t pending[SST_WORLD_KINDS][SST_WORLD_BYTES];
    uint8_t scene_receipts[SST_WORLD_BYTES]; // receipt bits never travel as ordinary switches
} SstWorld;
void sst_world_protect_scene(SstWorld *world, unsigned bit);
// Returns true only for newly admitted, set-only progress. Clear/unknown flags never propagate.
bool sst_world_receive(SstWorld *w, unsigned kind, unsigned bit, unsigned value);
bool sst_world_learn(SstWorld *w, unsigned kind, unsigned bit);
bool sst_world_pending(const SstWorld *w, unsigned kind, unsigned bit);
void sst_world_applied(SstWorld *w, unsigned kind, unsigned bit);
void sst_world_announce(const SstWorld *w);

#define SST_ENEMIES 256
typedef struct {
    bool used, seen, provisional, defeat_clear;
    uint64_t key;
    uint32_t instance;
    uint16_t profile, health;
} SstEnemyTrack;
typedef struct {
    SstScene scene;
    uint32_t load, sequence;
    // IDs outlive the concurrent roster. Fixed u16 coverage avoids exhausted sender
    // slots after repeated joins, without forgetting old duplicate/generation guards.
    // Keep this state static or on the heap (about 526 KiB including actor records).
    uint8_t sender[65536 / 8];
    uint32_t received[65536];
    uint32_t sender_generation[65536];
    SstEnemyTrack actors[SST_ENEMIES];
} SstEnemies;
typedef void (*SstEnemySend)(const SstEnemyEvent *, void *);
void sst_enemies_begin(SstEnemies *s, const SstScene *scene, uint32_t load);
// Called BEFORE receiving damage, on the guest thread; records local hits only.
void sst_enemies_observe(SstEnemies *s, uint64_t key, uint32_t instance, uint16_t profile,
                         uint16_t health, SstEnemySend send, void *user);
// Runtime observation also announces each newly spawned placed actor once. Peers with
// lower health answer that snapshot, including retained defeats in the same area visit.
// A first zero is provisional: wait for a positive native baseline before publishing
// its health or treating a later zero as a confirmed local defeat.
void sst_enemies_observe_live(SstEnemies *s, uint64_t key, uint32_t instance, uint16_t profile,
                             uint16_t health, SstEnemySend send, void *user);
// Returns the actor to mutate, with its reconciled health. Duplicates and other rooms are ignored.
SstEnemyTrack *sst_enemies_receive(SstEnemies *s, const SstEnemyEvent *event);
SstEnemyTrack *sst_enemies_exchange(SstEnemies *s, const SstEnemyEvent *event,
                                  SstEnemySend reply, void *user);
void sst_enemies_announce(SstEnemies *s, SstEnemySend send, void *user);
// A native defeat bit can confirm a removed actor if this same instance was
// previously seen alive with the bit clear. Missing actors alone prove nothing.
void sst_enemies_defeat_flags(SstEnemies *s, const uint8_t *flags, size_t size,
                              SstEnemySend send, void *user);

// Hold same-visit damage while native menus/events suspend actor mutation. Bounded FIFO;
// the caller clears it for offline/disabled/boss/trial states, and begins every valid tick.
#define SST_ENEMY_PENDING 512
typedef struct {
    SstScene scene;
    uint32_t load;
    unsigned count;
    bool overflow;
    SstEnemyEvent events[SST_ENEMY_PENDING];
    uint32_t instances[SST_ENEMY_PENDING];
} SstEnemyInbox;
void sst_enemy_inbox_begin(SstEnemyInbox *q, const SstScene *scene, uint32_t load);
bool sst_enemy_inbox_push(SstEnemyInbox *q, const SstEnemyEvent *event, uint32_t instance);
// Delivers in arrival order, clears the FIFO, and returns whether a refresh is needed.
typedef void (*SstHeldEnemyFn)(const SstEnemyEvent *, uint32_t instance, void *);
bool sst_enemy_inbox_flush(SstEnemyInbox *q, SstHeldEnemyFn apply, void *user);


// Render/UI -> guest-thread mailbox. Boss sync is permanently disabled.
typedef enum { SST_RECOVER_NONE, SST_RECOVER_SYNC, SST_RECOVER_RELOAD, SST_RECOVER_PEER } SstRecovery;
void sst_world_configure(bool progress, bool enemies);
void sst_world_options(bool *progress, bool *enemies);
// Repeated state refreshes preserve a queued navigation and its original deadline.
bool sst_world_merge_request(SstRecovery *pending, uint16_t *target, SstRecovery action, uint16_t peer);
void sst_world_request(SstRecovery action, uint16_t peer);
SstRecovery sst_world_take_request(uint16_t *peer);
void sst_world_status_set(bool boss, unsigned applied, unsigned enemies, const char *message);
void sst_world_status(char *out, size_t cap, bool *boss);
typedef enum { SST_REWARDS_READY, SST_REWARDS_OFFLINE, SST_REWARDS_LOADING,
    SST_REWARDS_DISABLED, SST_REWARDS_BOSS, SST_REWARDS_EVENT, SST_REWARDS_ACTION, SST_REWARDS_MENU, SST_REWARDS_TRIAL } SstRewardPause;
typedef struct {
    unsigned pending, other_dungeon, full_keys, conflicts, quest_choice, quest_ambiguous, pouch_wait, shop_wait, shop_order, sword_wait, song_wait;
    SstRewardPause pause;
} SstRewardStatus;
void sst_world_rewards_set(SstRewardStatus status);
SstRewardStatus sst_world_rewards(void);
bool sst_world_in_boss(void);
void sst_world_set_event(bool active);
uint8_t sst_world_pose_flags(void); // bit 7: boss encounter; bit 6: event/cutscene/native menu active
void sst_world_reset(void);
#ifdef __cplusplus
}
#endif

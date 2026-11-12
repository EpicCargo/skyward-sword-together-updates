// Skyward Sword Together: game-agnostic network client (docs/protocol.md).
// A background thread owns the socket and reconnects with backoff; the game thread only calls the
// non-blocking functions below. Everything here is thread safe.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SST_PROTO 45
#define SST_MAX_PLAYERS 16
#define SST_STR 64
// Pose-context bits are independent of sword(0..1) and world/motion(5..7).
// Existing protocol45 transports preserve this byte; old clients ignore these bits.
#define SST_POSE_CONTEXT_MASK 0x1c
#define SST_POSE_CONTEXT_SMALL_CARRY 0x04
#define SST_POSE_CONTEXT_UNSUPPORTED 0x1c
// Final-pose cache identity also distinguishes the existing DRAWN sword bit1.
// Ownership bit0 and world/motion bits5..7 do not change pose identity.
#define SST_POSE_IDENTITY_MASK (SST_POSE_CONTEXT_MASK | 0x02)


typedef struct {
    char host[128];
    uint16_t port;
    char name[SST_STR], room[SST_STR], team[SST_STR];
    bool auto_reconnect;
    bool use_udp; // opt-in hybrid movement, automatic TCP fallback
} SstConfig;

typedef struct {
    uint32_t tick;
    float pos[3];
    int16_t rot[3];
    uint16_t anim_id;
    float anim_frame;
    uint16_t action;
    uint16_t health, max_health;
    uint8_t equip_flags;
} SstPose;

typedef struct {
    char stage[SST_STR];
    uint8_t room, layer, entrance;
} SstScene;

typedef enum { SST_OFFLINE, SST_CONNECTING, SST_ONLINE, SST_FAILED } SstStatus;

// Remote player as last heard from the relay.
typedef struct {
    bool active;
    uint16_t id;
    uint32_t presence_generation; // changes on JOIN/SCENE even when ID and area stay the same
    char name[SST_STR], team[SST_STR];
    SstScene scene;
    bool has_pose;
    SstPose pose;
    uint64_t pose_time_ms;  // local receive time of `pose`
    uint32_t udp_scene; // reliable host-assigned scene incarnation; never a game address
} SstPlayer;

typedef void (*SstFlagFn)(uint8_t kind, uint16_t index, uint8_t value, uint16_t from, void *user);

void sst_net_start(const SstConfig *cfg);
void sst_net_stop(void);
SstStatus sst_net_status(char *why, int why_size);  // why: last error text (may be empty)
uint16_t sst_net_my_id(void);
uint32_t sst_net_generation(void); // changes after every successful handshake, including reconnects
typedef struct {
    bool online, has_rtt;
    unsigned rtt_ms;
    uint64_t last_packet_age_ms, queued_bytes;
    unsigned queue_recoveries; // automatic team refreshes after incoming backlog, this connection
    bool udp_available, udp_active;
    unsigned udp_sent, udp_received, udp_fallbacks;
} SstConnectionStats;
// Host/relay round trip, not the latency between every pair of players. Cleared on reconnect.
SstConnectionStats sst_net_stats(void);

// Local diagnostics only: TCP reachability and protocol handshakes are distinct phases.
typedef enum {
    SST_PHASE_IDLE, SST_PHASE_RESOLVE, SST_PHASE_TCP_CONNECT, SST_PHASE_HANDSHAKE, SST_PHASE_ONLINE,
    SST_PHASE_BACKOFF,
} SstNetPhase;
typedef enum {
    SST_FAIL_NONE, SST_FAIL_ADDRESS, SST_FAIL_RESOLVE, SST_FAIL_CONNECT_TIMEOUT, SST_FAIL_CONNECT_REFUSED,
    SST_FAIL_CONNECT_UNREACHABLE, SST_FAIL_CONNECT_OTHER, SST_FAIL_HANDSHAKE_TIMEOUT, SST_FAIL_REJECTED,
    SST_FAIL_CLOSED, SST_FAIL_SILENT, SST_FAIL_SOCKET,
} SstNetFailure;
typedef struct {
    SstStatus status;
    SstNetPhase phase;
    SstNetFailure failure;
    char host[128];
    uint16_t port, my_id, protocol;
    bool auto_reconnect, use_udp;
    char last_error[160];
    unsigned attempts, consecutive_failures, handshakes;
    uint64_t next_retry_in_ms;
    bool has_handshake;
    uint64_t since_handshake_ms;
    unsigned connect_timeout_ms, handshake_timeout_ms;
    unsigned udp_probes_sent, udp_probes_acked;
    unsigned enemy_events, motion_updates;
    bool has_enemy_rx, has_motion_rx;
    uint64_t enemy_rx_age_ms, motion_rx_age_ms;
} SstNetDiag;
SstNetDiag sst_net_diag(void);
const char *sst_net_phase_name(SstNetPhase phase);
const char *sst_net_failure_hint(SstNetFailure failure);
// Visible OFFLINE error for a rejected address, without disturbing an active connection.
void sst_net_set_offline_error(SstNetFailure failure, const char *text);

void sst_net_send_scene(const SstScene *s);
void sst_net_send_pose(const SstPose *p);  // droppable: skipped while offline
void sst_net_send_flag(uint8_t kind, uint16_t index, uint8_t value);  // queued while offline, sent on (re)connect

// Copies the current remote players into out (up to max); returns how many.
int sst_net_players(SstPlayer *out, int max);
bool sst_net_can_catchup(const SstPlayer *peer);
bool sst_net_can_request_catchup(const SstPlayer *peer);
// Delivers flags received since the last call (on the calling thread).
void sst_net_poll_flags(SstFlagFn fn, void *user);

// Short-lived regular-enemy state. The area travels with the event so queued packets cannot
// damage an actor in the room loaded afterwards. Never replayed from the host's world cache.
typedef struct {
    SstScene scene;
    uint64_t key;
    uint32_t sequence;
    uint16_t profile, damage, health, from;
    bool snapshot;
    uint32_t presence_generation; // local sender incarnation; never serialized
} SstEnemyEvent;
typedef void (*SstEnemyFn)(const SstEnemyEvent *, void *);
void sst_net_send_enemy(const SstEnemyEvent *event);
void sst_net_poll_enemies(SstEnemyFn fn, void *user);
void sst_net_request_world(void); // cached progress plus fresh announcements from teammates
// A host-acknowledged cache replay. Ready means its flags/receipts have been drained
// by the game consumer; local reward holds and live teammate announcements are separate.
typedef enum { SST_REFRESH_INVALID, SST_REFRESH_WAITING, SST_REFRESH_READY } SstWorldRefresh;
uint32_t sst_net_request_world_tracked(void); // zero while offline or send queue is stalled
SstWorldRefresh sst_net_world_refresh(uint32_t token);
bool sst_net_take_world_request(void);

typedef struct {
    SstScene scene;
    uint64_t key;
    uint32_t sequence;
    uint16_t profile, from;
    float pos[3], velocity[3];
    int16_t rot[3];
    uint64_t received_ms; // local receive time; never serialized
    uint32_t presence_generation; // local sender incarnation; never serialized
} SstEnemyMotion;
typedef void (*SstMotionFn)(const SstEnemyMotion *, void *);
void sst_net_send_motion(const SstEnemyMotion *motion); // live/droppable; never queued offline
void sst_net_poll_motion(SstMotionFn fn, void *user);
uint16_t sst_net_motion_owner(const SstScene *scene, bool local_eligible);

// Optional protocol-45 extensions (legacy vase0x32, generalized props0x34). Props use reliable
// scene/team-scoped events, without host world-cache replay or assets on the wire.
typedef struct SstProp SstProp;
typedef void (*SstPropFn)(const SstProp *, void *);
void sst_net_send_prop(const SstProp *prop); // bounded live stream; never queued offline
void sst_net_poll_props(SstPropFn fn, void *user); // authenticated arrival/generation attached

// Reliable pickup request0x33; the host sends distinct commit/snapshot envelopes.
// No pickup survives reconnect/offline queueing. Collector ACKs are delivered too.
typedef struct SstPickup SstPickup;
typedef struct SstPickupCommit SstPickupCommit;
typedef void (*SstPickupFn)(const SstPickupCommit *, void *);
bool sst_net_send_pickup(const SstPickup *pickup); // true only when queued on this live connection
void sst_net_poll_pickups(SstPickupFn fn, void *user);

#include "sst_chest.h"
void sst_net_send_chest(const SstChestReward *reward);
void sst_net_poll_chests(SstChestFn fn, void *user);

uint64_t sst_now_ms(void);

#ifdef __cplusplus
}
#endif

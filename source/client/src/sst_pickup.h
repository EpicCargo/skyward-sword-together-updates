// Session-only consumed-pickup claims. Only the collector receives native effects;
// peers record consumption/remove matching pickups. No addresses, instances or assets on wire.
#pragma once
#include "sst_net.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SST_PICKUP_AREAS 16
#define SST_PICKUPS_PER_AREA 256
#define SST_PICKUP_RECORDS 4096
#define SST_PICKUP_WIRE_MAX 96
#define SST_PICKUP_LOCAL_FRESH_MS 5000
#define SST_PICKUP_DROP_PARAMS 0x001bfc00u

typedef struct SstPickup {
    SstScene scene;
    uint16_t placed_id; // 0xffff means a source-only dynamic drop
    uint32_t params; // class0 placed permanent0..127/FF, dynamicFF, or exact native spawnDrop class1
    uint16_t item;
    uint32_t sequence;
    uint64_t token; // zero for placed items; nonzero opaque source token for dynamic drops
} SstPickup;
typedef struct SstPickupCommit {
    SstPickup pickup;
    uint32_t epoch; // nonzero host-issued area incarnation
    uint16_t from; // host-authenticated winning collector
    bool snapshot; // consumed-source announcement only: never award inventory
    uint32_t presence_generation; // transport-local; never serialized
    uint64_t received_ms; // transport-local; never serialized
} SstPickupCommit;
typedef void (*SstPickupFn)(const SstPickupCommit *, void *);

// Consumed-only registry: rupees2/3/4/32/33, Rupoor34, heart6, arrows7/8, bombs40/41,
// stamina42, seeds57/60, insects141..152, materials161..176. Legacy rupee/material
// counter/amount mappings remain available; other IDs return UINT_MAX/0. These
// values do NOT authorize a remote inventory effect: all native gain stays local.
bool sst_pickup_item(uint16_t item, unsigned *counter, int *amount);
// Permanent scene flags0..127 are accepted only for placed class0 actors. The receiver
// derives its own receipt table from the matching local actor; no inventory effect or table travels.
bool sst_pickup_valid(const SstPickup *pickup);
bool sst_pickup_commit_valid(const SstPickupCommit *commit);
// Key ignores entrance/item/sequence. Placed key=area+placedID+params;
// dynamic key=area+authenticated sourceID+opaque token. Conflicting rewards reject separately.
// Approved class0 generic61..64 material outcomes share one consumed identity even
// when separate native collectors resolve different items; their local rewards stay personal.
bool sst_pickup_key_equal(const SstPickup *a, uint16_t from_a, const SstPickup *b, uint16_t from_b);
size_t sst_pickup_encode(const SstPickup *pickup, uint8_t *out, size_t capacity);
bool sst_pickup_decode(const uint8_t *data, size_t size, SstPickup *out);
size_t sst_pickup_commit_encode(const SstPickupCommit *commit, uint8_t *out, size_t capacity);
bool sst_pickup_commit_decode(const uint8_t *data, size_t size, SstPickupCommit *out);

typedef struct { bool used; SstScene scene; uint32_t epoch; unsigned count; } SstPickupArea;
typedef struct { bool used; uint16_t area; SstPickupCommit commit; } SstPickupHostRecord;
typedef struct {
    uint32_t serial;
    SstPickupArea areas[SST_PICKUP_AREAS];
    SstPickupHostRecord records[SST_PICKUP_RECORDS];
} SstPickupHost;
typedef enum {
    SST_PICKUP_CLAIM_INVALID, SST_PICKUP_CLAIM_NEW, SST_PICKUP_CLAIM_DUPLICATE, SST_PICKUP_CLAIM_FULL
} SstPickupClaim;
void sst_pickup_host_reset(SstPickupHost *host);
// No eviction. Returns0 for an invalid scene or16 occupied area slots. Call on SCENE.
uint32_t sst_pickup_host_begin(SstPickupHost *host, const SstScene *scene);
// Occupied scenes are authenticated current peers; duplicate scenes are fine. Call on SCENE/drop.
// Clear an area's records only when no peer remains there. Invalid input never clears anything.
void sst_pickup_host_prune(SstPickupHost *host, const SstScene *occupied, size_t count);
// Caller authenticates area/team/presence/sequence and freshness. Area must already be begun.
// NEW emits a consumed-source commit. DUPLICATE returns the original receipt as snapshot=true.
// Malformed/conflicting/full requests do not mutate host or out. Never evict active receipts.
SstPickupClaim sst_pickup_host_claim(SstPickupHost *host, uint16_t from,
                                    const SstPickup *pickup, SstPickupCommit *out);
void sst_pickup_host_snapshots(const SstPickupHost *host, const SstScene *scene, SstPickupFn fn, void *user);

typedef struct {
    bool used, local, has_commit, applied;
    uint16_t source, local_source;
    uint32_t local_sequence;
    uint64_t local_ms;
    SstPickupCommit commit;
} SstPickupClientRecord;
typedef struct {
    bool initialized;
    SstScene scene;
    uint32_t epoch;
    SstPickupClientRecord records[SST_PICKUP_RECORDS];
} SstPickupState;
typedef enum {
    SST_PICKUP_INVALID, SST_PICKUP_NEW, SST_PICKUP_SEEN, SST_PICKUP_LOCAL,
    SST_PICKUP_SNAPSHOT, SST_PICKUP_FULL
} SstPickupDecision;
void sst_pickup_reset(SstPickupState *state);
// Current collection/visibility context only. Global cross-area receipt/pending records survive
// every begin(). Epoch0 permits local proof before host context; markers survive until ACK/reset.
bool sst_pickup_begin(SstPickupState *state, const SstScene *scene, uint32_t epoch);
// Record a completed local native collection BEFORE its host commit, including simultaneous
// collectors of a placed key. The sequence is local proof, not the host winner's sequence.
bool sst_pickup_local(SstPickupState *state, uint16_t mine, const SstPickup *pickup, uint64_t now_ms);
// NEW means consumption processing is still pending, including duplicate arrivals while pending.
// LOCAL/SNAPSHOT record consumption; SEEN means processing/no-award was already recorded.
// Every decision is inventory-neutral; the caller must never grant peers this pickup's reward.
// Live commits from every area are accepted. Dedup key includes source epoch+scene+canonical key.
// Malformed/conflicting/full input does not mutate state. Unknown-epoch local proof suppresses a
// different collector's simultaneous commit only within LOCAL_FRESH_MS; exact source/sequence
// ACK or known equal epoch stays valid. Transport authenticates each incoming area incarnation.
SstPickupDecision sst_pickup_accept(SstPickupState *state, const SstPickupCommit *commit, uint64_t now_ms);
// After caller records/processes consumption. Failure leaves NEW pending for a safe frame.
bool sst_pickup_applied(SstPickupState *state, const SstPickupCommit *commit);
void sst_pickup_pending(const SstPickupState *state, SstPickupFn fn, void *user);

#ifdef __cplusplus
}
#endif

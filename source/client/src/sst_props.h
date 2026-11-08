// Bounded reviewed carried-object transport/state. No assets or native addresses.
#pragma once
#include "sst_net.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SST_PROPS 32
#define SST_PROP_PROFILE 0x1f0
#define SST_PROP_BIG_PROFILE 0x1f1
#define SST_PROP_BARREL_PROFILE 0x209
#define SST_PROP_FRESH_MS 1500
#define SST_PROP_WIRE_MAX 110
typedef enum { SST_PROP_CARRY = 1, SST_PROP_PLACE = 2 } SstPropPhase;
typedef struct {
    uint16_t placed_id, profile;
    uint32_t params;
    uint8_t room, layer;
} SstPropKey;
typedef struct SstProp {
    SstScene scene;
    SstPropKey key;
    uint32_t sequence, pose_tick;
    SstPropPhase phase;
    float origin[3];
    int16_t yaw;
    float matrix[12], pos[3];
    int16_t rot[3];
    uint16_t from; // transport-authenticated sender; never serialized
    uint32_t presence_generation; // transport-local sender incarnation; never serialized
    uint64_t received_ms; // transport-local arrival clock; never serialized
} SstProp;
bool sst_prop_key_equal(const SstPropKey *a, const SstPropKey *b);
// Exact reviewed profiles. Barrel low nibble0/F is Normal; NPC quest modes
// (params bits6..7 <=1), bomb, Type2 and Kraken barrels remain excluded.
bool sst_prop_profile_allowed(uint16_t profile, uint32_t params);
bool sst_prop_valid(const SstProp *p);
// Little-endian body; exact consumed length is required. Decode leaves output unchanged on error.
size_t sst_prop_encode(const SstProp *p, uint8_t *out, size_t capacity);
bool sst_prop_decode(const uint8_t *data, size_t size, SstProp *out);

typedef struct { bool seen; uint32_t sequence; } SstPropSequence;
typedef struct {
    bool used, has_target;
    SstProp target;
    uint32_t owner_generation;
    uint64_t received_ms;
    SstPropSequence senders[SST_MAX_PLAYERS];
} SstPropTrack;
typedef struct { bool used, active; uint16_t id; uint32_t generation; } SstPropPeer;
typedef struct { uint16_t id; uint32_t generation; } SstPropPresence;
typedef struct {
    bool initialized, roster_bound;
    SstScene scene;
    uint32_t generation; // receiver's area/load incarnation, not sent
    SstPropPeer peers[SST_MAX_PLAYERS];
    SstPropTrack props[SST_PROPS];
} SstPropState;
void sst_props_reset(SstPropState *s);
// Changing room/layer/stage or local incarnation drops every target and replay watermark.
void sst_props_begin(SstPropState *s, const SstScene *scene, uint32_t generation);
// Bind the complete authenticated current-area roster, including the local sender.
// Unknown/departed incarnations cannot allocate slots after this call. Reordering
// preserves current streams' targets/watermarks; replacement clears only retired
// streams. Invalid input leaves state unchanged. An explicit roster authorizes its
// entries; packet updates alone never reauthorize a forgotten or departed peer.
bool sst_props_roster(SstPropState *s, const SstPropPresence *live, size_t count);
// Caller authenticates sender/team/scene incarnation. source_generation is the local presence
// incarnation attached by transport, never a field trusted from the wire. Lowest fresh carrying
// sender wins conflicts. PLACE needs the current owner or no target (initial announcement);
// a new CARRY may acquire a placed vase. Sequence is monotonic per sender and exact prop key.
bool sst_props_update(SstPropState *s, uint16_t sender, uint32_t source_generation,
                      const SstProp *p, uint64_t now_ms);
// CARRY expires after 1500ms; accepted PLACE lasts until begin/reset/forget or new acquisition.
bool sst_props_get(const SstPropState *s, const SstPropKey *key, uint64_t now_ms, SstProp *out);
// Marks this sender incarnation disconnected; late packets from it cannot reactivate targets.
void sst_props_forget(SstPropState *s, uint16_t sender, uint32_t source_generation);

#ifdef __cplusplus
}
#endif

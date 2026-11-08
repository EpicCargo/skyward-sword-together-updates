// Offline-verified SSHD destination metadata only; no warp/network executor.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SST_DESTINATION_LIMIT 32768u
#define SST_DESTINATION_QUERY_MAX 128u
enum {
    SST_DEST_VERIFIED = 1u << 0, // native PLY + declared logical layer / base resources
    SST_DEST_UNTESTED = 1u << 1, // metadata existence is not a runtime arrival test
    SST_DEST_STORY_DEPENDENT = 1u << 2, // current progress may affect entry behavior
    SST_DEST_ALTERNATE_LAYER = 1u << 3,
    SST_DEST_DUPLICATE_SPAWN = 1u << 4, // same native declaration across layers
    SST_DEST_BOSS_ARENA = 1u << 5, // catalogue only; never enables boss sync
    SST_DEST_EVENT_OR_TITLE = 1u << 6,
    SST_DEST_UNNAMED = 1u << 7,
    SST_DEST_REFERENCED = 1u << 8, // explicit native SCEN/public named entrance
    SST_DEST_NO_SPAWN = 1u << 9, // room-zone only: no proved direct/fallback arrival
    SST_DEST_SHARED_RESOURCES = 1u << 10, // declared logical layer shares base archive
    SST_DEST_FALLBACK_FIRST = 1u << 11 // request255 selects first PLY; native ID is u16FFFF
};
typedef struct {
    char stage[9];
    uint8_t room, layer, entrance; // requested byte; FALLBACK_FIRST is not a PLY ID255
    uint16_t flags;
    const char *region, *zone, *label;
} SstDestination;
typedef struct {
    char stage[9];
    uint8_t room;
    uint16_t flags;
    const char *region, *zone;
    size_t first, count; // contiguous destination range; count0 must stay disabled
} SstDestinationZone;

// Immutable catalogue pointers remain valid for the process lifetime.
size_t sst_destination_count(void);
const SstDestination *sst_destination_at(size_t index);
const SstDestination *sst_destination_find(const char *stage, unsigned room, unsigned layer, unsigned entrance);
// Existence only; does not certify safe arrival/current progress or execute a warp.
bool sst_destination_valid(const char *stage, unsigned room, unsigned layer, unsigned entrance);
size_t sst_destination_room_zone_count(void);
const SstDestinationZone *sst_destination_room_zone_at(size_t index);
// ASCII case-insensitive, all whitespace-separated tokens must match metadata.
// NULL/empty query means all. Query>128 or NULL output with nonzero capacity is
// invalid. Returns TOTAL matches; writes at most capacity indices. Flags must
// include required_flags and contain none of excluded_flags. No allocation/I/O.
size_t sst_destination_search(const char *query, uint16_t required_flags, uint16_t excluded_flags,
                              size_t *indices, size_t capacity);
// Bounded structural consistency check for offline-generated catalogue data.
bool sst_destination_catalog_valid(void);

#ifdef __cplusplus
}
#endif

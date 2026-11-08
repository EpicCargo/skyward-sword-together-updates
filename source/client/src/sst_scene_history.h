// Proof of scene changes already admitted by co-op, never a save-table snapshot.
#pragma once
#include "sst_world.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t bits[SST_WORLD_BYTES];
    bool dirty;
} SstSceneHistory;
typedef enum { SST_HISTORY_OK, SST_HISTORY_MISSING, SST_HISTORY_INVALID, SST_HISTORY_IO } SstHistoryResult;

// Only admitted, applied, non-receipt scene flags can become durable proof.
bool sst_scene_history_record(SstSceneHistory *history, const SstWorld *world, unsigned bit);
// A later policy/metadata decision can revoke old proof, never broaden it.
unsigned sst_scene_history_prune(SstSceneHistory *history, const SstWorld *world);
// Learn only proof still present in the local save. Does not queue local mutations.
unsigned sst_scene_history_recover(const SstSceneHistory *history,
                                  const uint8_t owned[SST_WORLD_BYTES], SstWorld *world);
// Fixed-size, versioned, checksummed file. Invalid/error loads leave memory unchanged.
SstHistoryResult sst_scene_history_load(SstSceneHistory *history, const char *path);
// Exclusive temporary file and atomic replacement; failures preserve the previous file.
bool sst_scene_history_save(SstSceneHistory *history, const char *path);
#ifdef __cplusplus
}
#endif

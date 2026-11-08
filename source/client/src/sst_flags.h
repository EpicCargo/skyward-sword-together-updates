// Shared-progress engine over plain byte arrays ("flag spaces": story flags, scene flags, ...).
// Detects bits the local game set since the last tick and announces them; applies teammates' bits as
// a one-way OR, so the network can never clear progress. Game-agnostic: the game layer registers the
// arrays (real guest memory in the game, plain buffers in tests).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SST_FLAG_SPACES 16
#define SST_FLAG_SPACE_MAX 0x2000  // bytes: bit index must fit the protocol's u16

typedef void (*SstFlagSendFn)(uint8_t kind, uint16_t bit, void *user);

typedef struct {
    uint8_t *live;              // the game's array
    const uint8_t *sync_mask;   // 1 bits are shared; NULL = all (counters and local-only bits stay 0)
    size_t size;
    uint8_t snapshot[SST_FLAG_SPACE_MAX];
    bool used;
} SstFlagSpace;

typedef struct {
    SstFlagSpace space[SST_FLAG_SPACES];
    bool primed;               // false until the first tick after a save load (baseline taken, nothing sent)
    uint32_t sent, applied;
} SstFlags;

void sst_flags_init(SstFlags *f);
bool sst_flags_register(SstFlags *f, uint8_t kind, uint8_t *live, size_t size, const uint8_t *sync_mask);
// The game loaded (or reloaded) a save: take a new baseline on the next tick instead of announcing
// everything in it as new.
void sst_flags_rebase(SstFlags *f);
// Call once per game frame: announces newly set shared bits.
void sst_flags_tick(SstFlags *f, SstFlagSendFn send, void *user);
// A teammate's bit. Returns true if it changed local state.
bool sst_flags_apply(SstFlags *f, uint8_t kind, uint16_t bit);
// Announces every shared bit that is set (after a (re)connect; the relay drops duplicates).
void sst_flags_announce_all(SstFlags *f, SstFlagSendFn send, void *user);

#ifdef __cplusplus
}
#endif

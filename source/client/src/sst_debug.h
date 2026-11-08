// Developer diagnostics: a bounded event log, opt-in detail switches and a bounded
// per-enemy receive table. No ImGui here; the Multiplayer tab renders snapshots.
// Rare events (connects, failures, handshakes, transport changes) are always recorded so a
// failure is visible after the fact. Per-packet detail costs one atomic load while disabled.
#pragma once
#include "sst_net.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { SST_DBG_NET, SST_DBG_HOST, SST_DBG_PLAYER, SST_DBG_ENEMY, SST_DBG_CATEGORIES } SstDebugCategory;

#define SST_DEBUG_LOG_LINES 128
#define SST_DEBUG_LINE 120
typedef struct {
    uint64_t at_ms;   // sst_now_ms() when recorded
    uint32_t serial;  // monotonically increasing; gaps mean older lines were overwritten
    uint8_t category;
    char text[SST_DEBUG_LINE];
} SstDebugLine;

void sst_debug_log(SstDebugCategory category, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
// Recorded only while detail is enabled for the category (e.g. per enemy update).
void sst_debug_detail(SstDebugCategory category, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
bool sst_debug_detail_enabled(SstDebugCategory category);
void sst_debug_set_detail(SstDebugCategory category, bool enabled);
// Copies up to `max` most recent lines, oldest first. `dropped` receives the number of lines
// overwritten since the last clear.
int sst_debug_lines(SstDebugLine *out, int max, uint32_t *dropped);
void sst_debug_clear(void);
const char *sst_debug_category_name(SstDebugCategory category);

// Enemy receive table, filled by the network thread only while SST_DBG_ENEMY detail is on.
#define SST_DEBUG_ENEMIES 64
typedef struct {
    uint64_t key;
    uint16_t profile, from;           // from: sender player id (the area's motion owner for MOTION)
    SstScene scene;
    bool has_motion, has_health;
    uint32_t motion_sequence, enemy_sequence;
    float pos[3];
    uint16_t health, damage;
    bool snapshot;
    uint64_t motion_at_ms, enemy_at_ms; // local receive times, 0 if never
} SstDebugEnemy;
void sst_debug_enemy_motion(const SstEnemyMotion *m);
void sst_debug_enemy_event(const SstEnemyEvent *e);
int sst_debug_enemies(SstDebugEnemy *out, int max);
void sst_debug_enemies_clear(void);

#ifdef __cplusplus
}
#endif

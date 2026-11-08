// See sst_debug.h. Fixed-size storage only: no allocation, no file or network output.
#include "sst_debug.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static SstDebugLine ring[SST_DEBUG_LOG_LINES];
static uint32_t next_serial, cleared_serial;
static atomic_uint detail_mask;
static SstDebugEnemy enemies[SST_DEBUG_ENEMIES];
static int enemy_n;

static void record(SstDebugCategory category, const char *fmt, va_list ap) {
    if ((unsigned)category >= SST_DBG_CATEGORIES || !fmt) return;
    char text[SST_DEBUG_LINE] = {0};
    vsnprintf(text, sizeof text, fmt, ap); // truncates long lines
    for (unsigned i=0;text[i];i++) if ((unsigned char)text[i]<32) text[i]=' ';
    uint64_t now = sst_now_ms();
    pthread_mutex_lock(&mu);
    SstDebugLine *line = &ring[next_serial % SST_DEBUG_LOG_LINES];
    line->at_ms = now; line->serial = next_serial++; line->category = (uint8_t)category;
    memcpy(line->text, text, sizeof text);
    pthread_mutex_unlock(&mu);
}
void sst_debug_log(SstDebugCategory category, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); record(category, fmt, ap); va_end(ap);
}
void sst_debug_detail(SstDebugCategory category, const char *fmt, ...) {
    if (!sst_debug_detail_enabled(category)) return;
    va_list ap; va_start(ap, fmt); record(category, fmt, ap); va_end(ap);
}
bool sst_debug_detail_enabled(SstDebugCategory category) {
    return (unsigned)category < SST_DBG_CATEGORIES && (atomic_load_explicit(&detail_mask, memory_order_relaxed) >> category & 1);
}
void sst_debug_set_detail(SstDebugCategory category, bool enabled) {
    if ((unsigned)category >= SST_DBG_CATEGORIES) return;
    if (enabled) atomic_fetch_or(&detail_mask, 1u << category);
    else atomic_fetch_and(&detail_mask, ~(1u << category));
    if (category == SST_DBG_ENEMY && !enabled) sst_debug_enemies_clear();
}
int sst_debug_lines(SstDebugLine *out, int max, uint32_t *dropped) {
    pthread_mutex_lock(&mu);
    uint32_t stored = next_serial - cleared_serial;
    uint32_t available = stored < SST_DEBUG_LOG_LINES ? stored : SST_DEBUG_LOG_LINES;
    if (dropped) *dropped = stored - available;
    uint32_t count = !out || max < 0 ? 0 : ((uint32_t)max < available ? (uint32_t)max : available);
    for (uint32_t i = 0; i < count; i++) out[i] = ring[(next_serial - count + i) % SST_DEBUG_LOG_LINES];
    pthread_mutex_unlock(&mu);
    return (int)count;
}
void sst_debug_clear(void) {
    pthread_mutex_lock(&mu); cleared_serial = next_serial; pthread_mutex_unlock(&mu);
}
const char *sst_debug_category_name(SstDebugCategory category) {
    static const char *const names[] = {"net", "host", "player", "enemy"};
    return (unsigned)category < SST_DBG_CATEGORIES ? names[category] : "?";
}

// Caller holds mu. Reuses the entry for `key` or the least recently updated one.
static SstDebugEnemy *enemy_slot(uint64_t key, uint16_t profile, const SstScene *scene) {
    SstDebugEnemy *oldest = NULL;
    for (int i = 0; i < enemy_n; i++) {
        if (enemies[i].key == key && enemies[i].profile==profile &&
            !strcmp(enemies[i].scene.stage,scene->stage) && enemies[i].scene.room==scene->room &&
            enemies[i].scene.layer==scene->layer) return &enemies[i];
        uint64_t at = enemies[i].motion_at_ms > enemies[i].enemy_at_ms ? enemies[i].motion_at_ms : enemies[i].enemy_at_ms;
        uint64_t best = !oldest ? UINT64_MAX : (oldest->motion_at_ms > oldest->enemy_at_ms ? oldest->motion_at_ms : oldest->enemy_at_ms);
        if (at < best) oldest = &enemies[i];
    }
    SstDebugEnemy *e = enemy_n < SST_DEBUG_ENEMIES ? &enemies[enemy_n++] : oldest;
    memset(e, 0, sizeof *e); e->key = key;
    return e;
}
void sst_debug_enemy_motion(const SstEnemyMotion *m) {
    if (!m || !sst_debug_detail_enabled(SST_DBG_ENEMY)) return;
    pthread_mutex_lock(&mu);
    if(!sst_debug_detail_enabled(SST_DBG_ENEMY)){pthread_mutex_unlock(&mu);return;}
    SstDebugEnemy *e = enemy_slot(m->key,m->profile,&m->scene);
    e->profile = m->profile; e->from = m->from; e->scene = m->scene;
    e->has_motion = true; e->motion_sequence = m->sequence;
    memcpy(e->pos, m->pos, sizeof e->pos); e->motion_at_ms = sst_now_ms();
    pthread_mutex_unlock(&mu);
}
void sst_debug_enemy_event(const SstEnemyEvent *ev) {
    if (!ev || !sst_debug_detail_enabled(SST_DBG_ENEMY)) return;
    pthread_mutex_lock(&mu);
    if(!sst_debug_detail_enabled(SST_DBG_ENEMY)){pthread_mutex_unlock(&mu);return;}
    SstDebugEnemy *e = enemy_slot(ev->key,ev->profile,&ev->scene);
    e->profile = ev->profile; e->scene = ev->scene; e->has_health = true;
    if (!e->has_motion) e->from = ev->from;
    e->enemy_sequence = ev->sequence; e->health = ev->health; e->damage = ev->damage;
    e->snapshot = ev->snapshot; e->enemy_at_ms = sst_now_ms();
    pthread_mutex_unlock(&mu);
}
int sst_debug_enemies(SstDebugEnemy *out, int max) {
    pthread_mutex_lock(&mu);
    int n = !out || max<=0 ? 0 : enemy_n < max ? enemy_n : max;
    if(n>0)memcpy(out,enemies,(size_t)n*sizeof *out);
    pthread_mutex_unlock(&mu);
    return n;
}
void sst_debug_enemies_clear(void) {
    pthread_mutex_lock(&mu); enemy_n = 0; pthread_mutex_unlock(&mu);
}

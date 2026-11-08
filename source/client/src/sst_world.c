#include "sst_world.h"
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "sst_story_policy.inc"

bool sst_world_song(unsigned item) { return item>=186 && item<=193; }
bool sst_world_boolean_definition(uint8_t word, uint8_t sw, unsigned words) {
    return word < words && (sw & 15) == 0; // low nibble is width minus one
}
bool sst_world_flag_allowed(unsigned k, unsigned b) {
    if (k == SST_WORLD_STORY) return b < 1202 && !sst_chest_quest_reserved(b) && !sst_chest_trial_reserved(b) &&
        (story_policy[b / 8] & (1u << (b % 8)));
    if (k == SST_WORLD_ITEM) {
        if (b >= 510) return false;
        if (sst_world_song(b)) return true;
        if (sst_chest_tool_gift(b)) return true;
        // Permanent equipment only. Pouch contents, bottles, materials, ammo, currency and counters
        // need item-grant transactions, and must never be treated as boolean ownership.
        switch (b) {
        case 9: case 10: case 11: case 12: case 13: case 14: case 15: case 16:
        case 56: case 68:
        case 95: case 96: case 97: case 98: case 99:
        case 112:
        case 138: case 177: case 178: case 179:
            return true;
        default: return false;
        }
    }
    // Only changes observed outside encounters enter the scene cache. Save-file snapshots are
    // deliberately not announced: a dungeon's scene table can also contain boss-room bits.
    if (k == SST_WORLD_SCENE)
        return b < SST_WORLD_BITS && !sst_chest_key_piece(SST_TBOX_CHESTS + b) &&
            !sst_chest_heart_pickup(SST_TBOX_CHESTS + b) &&
            !sst_chest_crystal_pickup(SST_TBOX_CHESTS + b) &&
            !sst_chest_saved_reward(SST_TBOX_CHESTS + b);
    // Dungeon items require their native reward transaction and receipt. Raw ownership copying
    // could otherwise grant a map/key while leaving its chest collectible. Kind 3 is reserved.
    return false;
}
bool sst_world_trial_stage(const char *s) {
    return !strcmp(s, "S000") || !strcmp(s, "S100") || !strcmp(s, "S200") || !strcmp(s, "S300");
}
bool sst_world_boss_stage(const char *s) {
    // B-prefixed arenas and scripted endgame areas. Overworld encounters are also detected by
    // their actors, so the ordinary Sealed Temple (F402) remains available for story progress.
    return s[0] == 'B' || !strcmp(s, "F403") || !strcmp(s, "F406");
}
bool sst_world_boss_profile(uint16_t p) {
    return (p >= 0xa5 && p <= 0xa9) || (p >= 0xb8 && p <= 0xba) ||
           (p >= 0xc3 && p <= 0xc5) || p == 0x147 || p == 0x14c || p == 0x14d ||
           (p >= 0x192 && p <= 0x198) || (p >= 0x19d && p <= 0x1a2) ||
           p == 0x167 || p == 0x168 || p == 0x171 || p == 0x18d || p == 0xab;
}
bool sst_world_boss_actor(uint16_t profile, uint8_t actor_room, uint8_t current_room) {
    // Some dungeons instantiate actors in multiple rooms. An unloaded room's miniboss must not
    // suspend ordinary co-op throughout the dungeon. Room -1 denotes a stage-wide actor.
    return sst_world_boss_profile(profile) && (actor_room == current_room || actor_room == 0xff);
}
bool sst_world_enemy_profile(uint16_t p) {
    switch (p) {
    case 0x14f: case 0x156: case 0x158: case 0x15a: case 0x15c:
    case 0x15f: case 0x162: case 0x163: case 0x164: case 0x166:
    case 0x169: case 0x16a: case 0x16d: case 0x16e: case 0x170:
    case 0x173: case 0x174: case 0x175: case 0x176: case 0x177:
    case 0x178: case 0x179: case 0x17a: case 0x17b: case 0x17c:
    case 0x17e: case 0x17f: case 0x180: case 0x181: case 0x182: case 0x184:
    case 0x185: case 0x186: case 0x187: case 0x188: case 0x18a:
    case 0x18b: case 0x18c: case 0x18e: case 0x190: return true;
    default: return false;
    }
}
bool sst_world_same_area(const SstScene *a, const SstScene *b) {
    return !strcmp(a->stage, b->stage) && a->room == b->room && a->layer == b->layer;
}
bool sst_world_catchup_request_allowed(const SstPlayer *p, const char *team) {
    if (!p || !team || !p->active || !p->id || strcmp(p->team, team)) return false;
    size_t n = strnlen(p->scene.stage, SST_STR);
    if (n < 2 || n > 7 || (p->scene.stage[0] != 'F' && p->scene.stage[0] != 'D') ||
        sst_world_boss_stage(p->scene.stage)) return false;
    for (size_t i = 1; i < n; i++)
        if (!isalnum((unsigned char)p->scene.stage[i]) && p->scene.stage[i] != '_') return false;
    return true;
}
SstCatchupStatus sst_world_catchup_status(const SstPlayer *p, const char *team, uint64_t now) {
    if (!p || !team || !p->active || !p->id) return SST_CATCHUP_UNAVAILABLE;
    if (strcmp(p->team, team)) return SST_CATCHUP_OTHER_TEAM;
    size_t n = strnlen(p->scene.stage, SST_STR);
    if (n < 2 || n > 7) return SST_CATCHUP_NO_AREA;
    if (sst_world_trial_stage(p->scene.stage)) return SST_CATCHUP_TRIAL;
    if (sst_world_boss_stage(p->scene.stage)) return SST_CATCHUP_BOSS;
    if (!sst_world_catchup_request_allowed(p, team)) return SST_CATCHUP_NO_AREA;
    // Cross-area poses can be old. Do not present their old health/action as current.
    if (!p->has_pose || now < p->pose_time_ms || now - p->pose_time_ms > 2000) return SST_CATCHUP_STALE;
    if (!p->pose.health) return SST_CATCHUP_DEFEATED;
    if (p->pose.health > p->pose.max_health || p->pose.max_health > 80) return SST_CATCHUP_INVALID_HEALTH;
    if (p->pose.equip_flags & 0x80) return SST_CATCHUP_BOSS;
    if (p->pose.equip_flags & 0x40) return SST_CATCHUP_MENU;
    if (p->pose.action != 0 && p->pose.action != 2 && p->pose.action != 3) return SST_CATCHUP_BUSY;
    return SST_CATCHUP_READY;
}
const char *sst_world_catchup_text(SstCatchupStatus status) {
    switch (status) {
    case SST_CATCHUP_READY: return "Ready";
    case SST_CATCHUP_OTHER_TEAM: return "Different team";
    case SST_CATCHUP_NO_AREA: return "Not in a playable area";
    case SST_CATCHUP_BOSS: return "Boss encounter";
    case SST_CATCHUP_STALE: return "Needs fresh teammate state";
    case SST_CATCHUP_DEFEATED: return "Defeated";
    case SST_CATCHUP_INVALID_HEALTH: return "Invalid teammate health";
    case SST_CATCHUP_MENU: return "Event or game menu";
    case SST_CATCHUP_BUSY: return "Waiting for normal movement";
    case SST_CATCHUP_TRIAL: return "Silent Realm trial";
    default: return "Teammate unavailable";
    }
}
bool sst_world_catchup_allowed(const SstPlayer *p, const char *team, uint64_t now) {
    return sst_world_catchup_status(p, team, now) == SST_CATCHUP_READY;
}
uint64_t sst_world_enemy_key(uint16_t p, uint16_t id, uint32_t params) {
    return (uint64_t)params << 32 | (uint64_t)p << 16 | id;
}
bool sst_world_learn(SstWorld *w, unsigned k, unsigned b) {
    if (!sst_world_flag_allowed(k, b)) return false;
    if (k == SST_WORLD_SCENE && (w->scene_receipts[b / 8] & (1u << (b % 8)))) return false;
    uint8_t *v = &w->known[k][b / 8], m = 1u << (b % 8);
    if (*v & m) return false;
    *v |= m; return true;
}
bool sst_world_receive(SstWorld *w, unsigned k, unsigned b, unsigned v) {
    if (v != 1 || !sst_world_flag_allowed(k, b)) return false;
    if (k == SST_WORLD_SCENE && (w->scene_receipts[b / 8] & (1u << (b % 8)))) return false;
    // Queue even known bits: a reload/rollback may have cleared the local copy.
    w->known[k][b / 8] |= 1u << (b % 8);
    w->pending[k][b / 8] |= 1u << (b % 8);
    return true;
}
void sst_world_protect_scene(SstWorld *w, unsigned b) {
    if (b >= SST_WORLD_BITS) return;
    uint8_t mask = 1u << (b % 8);
    w->scene_receipts[b / 8] |= mask;
    w->known[SST_WORLD_SCENE][b / 8] &= ~mask;
    w->pending[SST_WORLD_SCENE][b / 8] &= ~mask;
}
bool sst_world_pending(const SstWorld *w, unsigned k, unsigned b) {
    return sst_world_flag_allowed(k, b) && (w->pending[k][b / 8] & (1u << (b % 8)));
}
void sst_world_applied(SstWorld *w, unsigned k, unsigned b) {
    if (sst_world_flag_allowed(k, b)) w->pending[k][b / 8] &= ~(1u << (b % 8));
}
void sst_world_announce(const SstWorld *w) {
    for (unsigned k = 0; k < SST_WORLD_KINDS; k++)
        for (unsigned b = 0; b < SST_WORLD_BITS; b++)
            if (w->known[k][b / 8] & (1u << (b % 8))) sst_net_send_flag(k, b, 1);
}

void sst_enemies_begin(SstEnemies *s, const SstScene *scene, uint32_t load) {
    if (!sst_world_same_area(scene, &s->scene) || s->load != load) {
        // Sequence numbers remain monotonic while actor records reset for a new room visit.
        uint32_t seq = s->sequence;
        memset(s, 0, sizeof *s); s->sequence = seq; s->scene = *scene; s->load = load;
    }
    for (int i = 0; i < SST_ENEMIES; i++) s->actors[i].seen = false;
}
static void enemy_send(SstEnemies *s, SstEnemyTrack *a, unsigned damage, bool snapshot,
                       SstEnemySend send, void *user) {
    SstEnemyEvent e = {.scene = s->scene, .key = a->key, .sequence = ++s->sequence,
        .profile = a->profile, .damage = damage, .health = a->health, .snapshot = snapshot};
    send(&e, user);
}
static void observe_enemy(SstEnemies *s, uint64_t key, uint32_t instance, uint16_t profile,
                          uint16_t hp, SstEnemySend send, void *user, bool birth) {
    if (!sst_world_enemy_profile(profile) || hp > 1000) return;
    SstEnemyTrack *a = NULL, *slot = NULL;
    for (int i = 0; i < SST_ENEMIES; i++) {
        if (s->actors[i].used && s->actors[i].key == key) { a = &s->actors[i]; break; }
        if (!s->actors[i].used) slot = &s->actors[i];
    }
    if (!a) a = slot;
    if (!a) return;
    if (!a->used || a->instance != instance) {
        *a = (SstEnemyTrack){.used = true, .seen = true, .key = key, .instance = instance,
                             .profile = profile, .health = hp, .provisional = birth && !hp};
        if (birth && send && !a->provisional) enemy_send(s, a, 0, true, send, user);
        return;
    }
    a->seen = true;
    if (a->provisional) {
        if (birth && !hp) return;
        a->provisional=false; a->health=hp;
        if (birth && send) enemy_send(s,a,0,true,send,user);
        return;
    }
    unsigned damage = hp < a->health ? a->health - hp : 0;
    a->health = hp;
    if (damage) enemy_send(s, a, damage, false, send, user);
}
void sst_enemies_observe(SstEnemies *s, uint64_t key, uint32_t instance, uint16_t profile,
                         uint16_t hp, SstEnemySend send, void *user) {
    observe_enemy(s,key,instance,profile,hp,send,user,false);
}
void sst_enemies_observe_live(SstEnemies *s, uint64_t key, uint32_t instance, uint16_t profile,
                             uint16_t hp, SstEnemySend send, void *user) {
    if (!instance || (uint16_t)(key >> 16) != profile || (uint16_t)key == 0xffff ||
        sst_world_boss_stage(s->scene.stage) || sst_world_trial_stage(s->scene.stage)) return;
    observe_enemy(s,key,instance,profile,hp,send,user,true);
}
static SstEnemyTrack *receive_enemy(SstEnemies *s, const SstEnemyEvent *e, SstEnemySend reply, void *user) {
    if (!e->from || !sst_world_same_area(&s->scene, &e->scene) || !sst_world_enemy_profile(e->profile) ||
        e->health > 1000 || e->damage > 1000 || (e->snapshot && e->damage)) return NULL;
    unsigned n=e->from; uint8_t mask=(uint8_t)(1u << (n & 7));
    if (s->sender[n / 8] & mask) {
        int32_t generation = (int32_t)(e->presence_generation - s->sender_generation[n]);
        if (generation < 0 || (!generation && (int32_t)(e->sequence - s->received[n]) <= 0)) return NULL;
    }
    s->sender[n / 8] |= mask; s->received[n] = e->sequence;
    s->sender_generation[n] = e->presence_generation;
    for (int i = 0; i < SST_ENEMIES; i++) {
        SstEnemyTrack *a = &s->actors[i];
        if (!a->used || a->key != e->key || a->profile != e->profile) continue;
        if (a->provisional) return NULL;
        // A deleted actor's confirmed zero-health record remains useful until this visit ends.
        // Answer only a strictly higher snapshot; equal/lower replies cannot form an echo loop.
        if (reply && e->snapshot && e->health > a->health && (a->seen || !a->health))
            enemy_send(s,a,0,true,reply,user);
        if (!a->seen) return NULL;
        uint16_t hp = e->snapshot ? a->health : (e->damage < a->health ? a->health - e->damage : 0);
        if (e->health < hp) hp = e->health;
        if (hp >= a->health) return NULL;
        a->health = hp; // baseline follows the remote write, so it cannot echo as a local hit
        return a;
    }
    return NULL;
}
SstEnemyTrack *sst_enemies_receive(SstEnemies *s, const SstEnemyEvent *e) {
    return receive_enemy(s,e,NULL,NULL);
}
SstEnemyTrack *sst_enemies_exchange(SstEnemies *s, const SstEnemyEvent *e, SstEnemySend reply, void *user) {
    if ((uint16_t)(e->key >> 16) != e->profile || (uint16_t)e->key == 0xffff ||
        sst_world_boss_stage(e->scene.stage) || sst_world_trial_stage(e->scene.stage)) return NULL;
    return receive_enemy(s,e,reply,user);
}
void sst_enemies_announce(SstEnemies *s, SstEnemySend send, void *user) {
    for (int i = 0; i < SST_ENEMIES; i++)
        if (s->actors[i].used && !s->actors[i].provisional && (s->actors[i].seen || !s->actors[i].health))
            enemy_send(s, &s->actors[i], 0, true, send, user);
}

void sst_enemies_defeat_flags(SstEnemies *s, const uint8_t *flags, size_t size,
                              SstEnemySend send, void *user) {
    if (!flags || size != 8192 || !send ||
        (s->scene.stage[0]!='D' && s->scene.stage[0]!='F') ||
        sst_world_boss_stage(s->scene.stage) || sst_world_trial_stage(s->scene.stage)) return;
    for (unsigned i=0; i<SST_ENEMIES; i++) {
        SstEnemyTrack *a=&s->actors[i]; unsigned id=(uint16_t)a->key;
        if (!a->used || a->provisional || !a->health || id==0xffff ||
            (uint16_t)(a->key>>16)!=a->profile || !sst_world_enemy_profile(a->profile)) continue;
        unsigned word=id/16;
        bool defeated=((flags[2*word] | (unsigned)flags[2*word+1]<<8) & (1u<<(id%16)))!=0;
        if (a->seen) {
            if (!defeated) a->defeat_clear=true;
        } else if (defeated && a->defeat_clear) {
            a->health=0; a->defeat_clear=false;
            // The native flag proves defeat, not an observed amount of damage.
            enemy_send(s,a,0,true,send,user);
        }
    }
}


void sst_enemy_inbox_begin(SstEnemyInbox *q, const SstScene *scene, uint32_t load) {
    if (q->load != load || !sst_world_same_area(&q->scene, scene)) {
        memset(q, 0, sizeof *q); q->scene = *scene; q->load = load;
    }
}
bool sst_enemy_inbox_push(SstEnemyInbox *q, const SstEnemyEvent *e, uint32_t instance) {
    if (!instance || !e->from || !sst_world_same_area(&q->scene, &e->scene) ||
        !sst_world_enemy_profile(e->profile) || (uint16_t)(e->key >> 16) != e->profile ||
        (uint16_t)e->key == 0xffff || e->health > 1000 || e->damage > 1000 ||
        (e->snapshot && e->damage) || sst_world_boss_stage(e->scene.stage) ||
        (e->scene.stage[0] != 'D' && e->scene.stage[0] != 'F')) return false;
    for (unsigned i = 0; i < q->count; i++)
        if (q->events[i].from == e->from && q->events[i].presence_generation == e->presence_generation &&
            q->events[i].sequence == e->sequence) return false;
    if (q->count == SST_ENEMY_PENDING) { q->overflow = true; return false; }
    q->instances[q->count] = instance; q->events[q->count++] = *e; return true;
}
bool sst_enemy_inbox_flush(SstEnemyInbox *q, SstHeldEnemyFn apply, void *user) {
    for (unsigned i = 0; i < q->count; i++) apply(&q->events[i], q->instances[i], user);
    bool overflow = q->overflow; q->count = 0; q->overflow = false; return overflow;
}

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static bool progress_enabled = true, enemies_enabled = true, boss_active, event_active;
static SstRecovery request;
static uint16_t request_peer;
static char message[240] = "Waiting for gameplay";
static unsigned applied_count, enemy_count;
static SstRewardStatus reward_status = {.pause = SST_REWARDS_OFFLINE};
void sst_world_rewards_set(SstRewardStatus s) {
    pthread_mutex_lock(&mu); reward_status = s; pthread_mutex_unlock(&mu);
}
SstRewardStatus sst_world_rewards(void) {
    pthread_mutex_lock(&mu); SstRewardStatus s = reward_status; pthread_mutex_unlock(&mu); return s;
}
void sst_world_configure(bool p, bool e) { pthread_mutex_lock(&mu); progress_enabled = p; enemies_enabled = e; pthread_mutex_unlock(&mu); }
void sst_world_options(bool *p, bool *e) { pthread_mutex_lock(&mu); *p = progress_enabled; *e = enemies_enabled; pthread_mutex_unlock(&mu); }
bool sst_world_merge_request(SstRecovery *pending, uint16_t *target, SstRecovery r, uint16_t peer) {
    if (r < SST_RECOVER_SYNC || r > SST_RECOVER_PEER || (r == SST_RECOVER_PEER && !peer)) return false;
    if (r == SST_RECOVER_SYNC && *pending != SST_RECOVER_NONE) return false;
    if (r != SST_RECOVER_PEER) peer = 0;
    if (*pending == r && *target == peer) return false;
    *pending = r; *target = peer; return true;
}
void sst_world_request(SstRecovery r, uint16_t peer) {
    pthread_mutex_lock(&mu); sst_world_merge_request(&request, &request_peer, r, peer); pthread_mutex_unlock(&mu);
}
SstRecovery sst_world_take_request(uint16_t *peer) {
    pthread_mutex_lock(&mu); SstRecovery r = request; *peer = request_peer; request = SST_RECOVER_NONE; pthread_mutex_unlock(&mu); return r;
}
void sst_world_status_set(bool boss, unsigned applied, unsigned enemies, const char *msg) {
    pthread_mutex_lock(&mu); boss_active = boss; applied_count = applied; enemy_count = enemies;
    if (msg) snprintf(message, sizeof message, "%s", msg);
    pthread_mutex_unlock(&mu);
}
void sst_world_status(char *out, size_t cap, bool *boss) {
    pthread_mutex_lock(&mu);
    snprintf(out, cap, "%s | %u progress changes, %u enemies", message, applied_count, enemy_count);
    if (boss) *boss = boss_active;
    pthread_mutex_unlock(&mu);
}
bool sst_world_in_boss(void) { pthread_mutex_lock(&mu); bool b = boss_active; pthread_mutex_unlock(&mu); return b; }
void sst_world_set_event(bool active) { pthread_mutex_lock(&mu); event_active = active; pthread_mutex_unlock(&mu); }
uint8_t sst_world_pose_flags(void) {
    pthread_mutex_lock(&mu); uint8_t f = (boss_active ? 0x80 : 0) | (event_active ? 0x40 : 0); pthread_mutex_unlock(&mu); return f;
}
void sst_world_reset(void) {
    pthread_mutex_lock(&mu); request = SST_RECOVER_NONE; boss_active = event_active = false;
    applied_count = enemy_count = 0; snprintf(message, sizeof message, "Waiting for gameplay"); pthread_mutex_unlock(&mu);
    sst_world_rewards_set((SstRewardStatus){.pause = SST_REWARDS_OFFLINE});
}

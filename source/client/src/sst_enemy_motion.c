#include "sst_enemy_motion.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

static atomic_bool enabled = true;
void sst_motion_configure(bool v) { atomic_store(&enabled, v); }
bool sst_motion_enabled(void) { return atomic_load(&enabled); }

bool sst_motion_valid(const SstEnemyMotion *m) {
    if (!m) return false;
    size_t n = strnlen(m->scene.stage, SST_STR);
    if (!n || n > 7 || sst_world_boss_stage(m->scene.stage) ||
        (m->scene.stage[0] != 'D' && m->scene.stage[0] != 'F') ||
        !sst_world_enemy_profile(m->profile) || (uint16_t)(m->key >> 16) != m->profile ||
        (uint16_t)m->key == 0xffff) return false;
    for (int i = 0; i < 3; i++)
        if (!isfinite(m->pos[i]) || fabsf(m->pos[i]) > 1e6f ||
            !isfinite(m->velocity[i]) || fabsf(m->velocity[i]) > 1000) return false;
    return true;
}
static bool fresh(uint64_t t, uint64_t now, unsigned limit) { return t && t <= now && now - t <= limit; }
// Network-thread receive stamps may be a few ms newer than the game tick's clock.
static uint64_t age(uint64_t t, uint64_t now) { return t >= now ? 0 : now - t; }
static bool live(uint64_t t, uint64_t now) {
    return t && (t <= now ? now - t <= SST_MOTION_TIMEOUT_MS : t - now <= SST_MOTION_FUTURE_MS);
}

bool sst_motion_eligible(uint8_t flags, uint16_t health, uint64_t pose_ms, uint64_t now) {
    return health && (flags & 0xe0) == SST_MOTION_CAPABLE && fresh(pose_ms, now, SST_MOTION_FRESH_MS);
}
uint16_t sst_motion_pick_owner(const SstMotionCandidate *c, int count, const char *team,
                               const SstScene *area, uint64_t now) {
    if (!c || !team || !area || count <= 0 || count > SST_MAX_PLAYERS + 1) return 0;
    uint16_t owner = 0;
    for (int i = 0; i < count; i++) {
        if (!c[i].id || !c[i].team || !c[i].scene || !team || strcmp(c[i].team, team) ||
            !sst_world_same_area(c[i].scene, area) ||
            !sst_motion_eligible(c[i].pose_flags, c[i].health, c[i].pose_ms, now)) continue;
        if (!owner || c[i].id < owner) owner = c[i].id;
    }
    return owner;
}
uint16_t sst_motion_owner(uint16_t mine, bool eligible, const SstScene *scene, const char *team,
                          const SstPlayer *ps, int count, uint64_t now) {
    if (!scene || !team || count < 0 || (count && !ps)) return 0;
    SstMotionCandidate c[SST_MAX_PLAYERS + 1]; int n = 0;
    // The local player's own pose is current by definition; the caller decides its eligibility.
    if (eligible && mine)
        c[n++] = (SstMotionCandidate){mine, team, scene, now, SST_MOTION_CAPABLE, 1};
    for (int i = 0; i < count && n < SST_MAX_PLAYERS + 1; i++) {
        const SstPlayer *p = &ps[i];
        if (!p->active || !p->id || p->id == mine || !p->has_pose) continue;
        c[n++] = (SstMotionCandidate){p->id, p->team, &p->scene, p->pose_time_ms,
                                      p->pose.equip_flags, p->pose.health};
    }
    return sst_motion_pick_owner(c, n, team, scene, now);
}
void sst_motion_begin_at(SstMotionState *s, const SstScene *scene, uint32_t load, uint16_t owner, uint64_t now) {
    if (!s) return;
    if (!scene) { memset(s, 0, sizeof *s); return; }
    if (s->load != load || !sst_world_same_area(&s->scene, scene)) {
        memset(s, 0, sizeof *s); s->scene = *scene; s->load = load; s->owner = owner; s->owner_since_ms = now;
        return;
    }
    if (s->owner != owner) {
        // Handoff: keep every track. Displayed samples continue (interpolate/extrapolate/hold)
        // until the new owner's first packet blends from them, so nothing snaps.
        s->previous_owner = s->owner; s->owner = owner; s->owner_since_ms = now; s->owner_changes++;
    }
}
void sst_motion_begin(SstMotionState *s, const SstScene *scene, uint32_t load, uint16_t owner) {
    sst_motion_begin_at(s, scene, load, owner, sst_now_ms());
}
static SstMotionTrack *observe(SstMotionState *s, uint64_t key, uint32_t instance, uint16_t profile) {
    if (!s || !sst_world_enemy_profile(profile)) return NULL;
    SstMotionTrack *slot = NULL;
    for (unsigned i = 0; i < SST_ENEMIES; i++) {
        SstMotionTrack *a = &s->actors[i];
        if (a->used && a->key == key) { slot = a; break; }
        if (!a->used) slot = a;
    }
    if (slot && (!slot->used || slot->instance != instance))
        *slot = (SstMotionTrack){.used = true, .key = key, .instance = instance, .profile = profile,
                                 .interval_ms = SST_MOTION_INTERVAL_DEFAULT_MS};
    return slot;
}
void sst_motion_observe(SstMotionState *s, uint64_t key, uint32_t instance, uint16_t profile) {
    observe(s, key, instance, profile);
}
void sst_motion_observe_at(SstMotionState *s, uint64_t key, uint32_t instance, uint16_t profile,
                           const float pos[3], const int16_t rot[3]) {
    SstMotionTrack *a = observe(s, key, instance, profile);
    if (!a || !pos || !rot) return;
    for (int i = 0; i < 3; i++) if (!isfinite(pos[i]) || fabsf(pos[i]) > 1e6f) return;
    memcpy(a->local, pos, 12); memcpy(a->local_rot, rot, 6); a->has_local = true;
}
static float distance(const float a[3], const float b[3]) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}
static unsigned clamp_ms(uint64_t v, unsigned lo, unsigned hi) { return v < lo ? lo : v > hi ? hi : (unsigned)v; }
// Segment: interpolate start -> target over the measured interval, then dead-reckon along the
// measured rate for at most SST_MOTION_EXTRAP_MS, then hold. Rotation takes the shortest arc.
static void sample_track(const SstMotionTrack *a, uint64_t now, float p[3], int16_t r[3]) {
    unsigned window = clamp_ms(a->interval_ms, SST_MOTION_INTERP_MIN_MS, SST_MOTION_INTERP_MAX_MS);
    uint64_t e = age(a->base_ms, now);
    if (e < window) {
        float t = (float)e / (float)window;
        for (int i = 0; i < 3; i++) {
            p[i] = a->start[i] + (a->target[i] - a->start[i]) * t;
            int16_t delta = (int16_t)((uint16_t)a->target_rot[i] - (uint16_t)a->start_rot[i]);
            r[i] = (int16_t)(uint16_t)((uint16_t)a->start_rot[i] + (uint16_t)(int32_t)lrintf(delta * t));
        }
        return;
    }
    float x = (float)(e - window < SST_MOTION_EXTRAP_MS ? e - window : SST_MOTION_EXTRAP_MS);
    for (int i = 0; i < 3; i++) p[i] = a->target[i] + (a->has_rate ? a->rate[i] * x : 0);
    memcpy(r, a->target_rot, 6);
}
static bool reject(SstMotionState *s) { s->rejected++; return false; }
bool sst_motion_receive(SstMotionState *s, const SstEnemyMotion *m, uint64_t now) {
    if (!s || !m) return false;
    if (!s->owner || !m->from || !sst_world_same_area(&s->scene, &m->scene) ||
        !sst_motion_valid(m) || !live(m->received_ms, now)) return reject(s);
    // The previous owner's in-flight packets remain usable for its own tracks during a short
    // handoff grace (host and client freshness clocks can disagree by a tick plus latency).
    bool handoff = m->from != s->owner;
    if (handoff && (m->from != s->previous_owner || age(s->owner_since_ms, now) > SST_MOTION_HANDOFF_MS))
        return reject(s);
    for (unsigned i = 0; i < SST_ENEMIES; i++) {
        SstMotionTrack *a = &s->actors[i];
        if (!a->used || a->key != m->key || a->profile != m->profile) continue;
        if (handoff && (!a->has_target || a->from != m->from)) return reject(s);
        bool current = a->has_target && live(a->received_ms, now);
        bool same = a->has_target && a->from == m->from;
        if (same && a->presence_generation != m->presence_generation) {
            if ((int32_t)(m->presence_generation - a->presence_generation) < 0) return reject(s);
            // A reconnect is a new stream, not a continuation of the old peer's
            // movement. Blend from fresh native observation if available.
            same = false; current = false;
        }
        if (same && (int32_t)(m->sequence - a->sequence) <= 0) return reject(s);
        float cur[3]; int16_t cur_rot[3];
        if (current) sample_track(a, now, cur, cur_rot);
        else if (a->has_local) { memcpy(cur, a->local, 12); memcpy(cur_rot, a->local_rot, 6); }
        else { memcpy(cur, m->pos, 12); memcpy(cur_rot, m->rot, 6); }
        if (distance(cur, m->pos) > SST_MOTION_SNAP_DISTANCE) { memcpy(cur, m->pos, 12); memcpy(cur_rot, m->rot, 6); }
        a->has_rate = false;
        if (same && current && m->received_ms > a->received_ms) {
            uint64_t dt = m->received_ms - a->received_ms;
            unsigned measured = clamp_ms(dt, SST_MOTION_INTERP_MIN_MS, SST_MOTION_INTERP_MAX_MS);
            a->interval_ms = (a->interval_ms * 3 + measured + 2) / 4;
            if (distance(a->target, m->pos) <= SST_MOTION_SNAP_DISTANCE) {
                // Bunched arrivals must not inflate the speed: divide by at least the minimum window.
                float span = (float)clamp_ms(dt, SST_MOTION_INTERP_MIN_MS, SST_MOTION_TIMEOUT_MS);
                for (int k = 0; k < 3; k++) a->rate[k] = (m->pos[k] - a->target[k]) / span;
                a->has_rate = true;
            }
        }
        memcpy(a->start, cur, 12); memcpy(a->start_rot, cur_rot, 6);
        memcpy(a->target, m->pos, 12); memcpy(a->target_rot, m->rot, 6); memcpy(a->velocity, m->velocity, 12);
        a->base_ms = now; a->received_ms = m->received_ms; a->sequence = m->sequence; a->has_target = true;
        a->from = m->from; a->presence_generation = m->presence_generation;
        s->accepted++;
        return true;
    }
    return reject(s);
}
bool sst_motion_sample(const SstMotionState *s, uint64_t key, uint32_t instance, uint64_t now,
                        float pos[3], int16_t rot[3], float velocity[3]) {
    if (!s || !s->owner || !pos || !rot || !velocity) return false;
    for (unsigned i = 0; i < SST_ENEMIES; i++) {
        const SstMotionTrack *a = &s->actors[i];
        if (!a->used || !a->has_target || a->key != key || a->instance != instance ||
            !live(a->received_ms, now)) continue;
        sample_track(a, now, pos, rot); memcpy(velocity, a->velocity, 12); return true;
    }
    return false;
}

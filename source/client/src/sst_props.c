#include "sst_props.h"
#include <math.h>
#include <string.h>

static bool same_area(const SstScene *a, const SstScene *b) {
    return !strcmp(a->stage, b->stage) && a->room == b->room && a->layer == b->layer;
}
static size_t stage_length(const SstScene *s) {
    size_t n = 0;
    while (n < SST_STR && s->stage[n]) n++;
    if (!n || n > 7) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s->stage[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return n;
}
static bool bounded(float f) { return isfinite(f) && fabsf(f) <= 1000000.0f; }
static bool newer(uint32_t a, uint32_t b) { uint32_t d = a - b; return d && d < 0x80000000u; }
static bool fresh(uint64_t at, uint64_t now) { return now >= at && now - at <= SST_PROP_FRESH_MS; }
bool sst_prop_key_equal(const SstPropKey *a, const SstPropKey *b) {
    return a && b && a->placed_id == b->placed_id && a->profile == b->profile &&
        a->params == b->params && a->room == b->room && a->layer == b->layer;
}
bool sst_prop_profile_allowed(uint16_t profile, uint32_t params) {
    if (profile == SST_PROP_PROFILE || profile == SST_PROP_BIG_PROFILE) return true;
    unsigned type = params & 15u;
    return profile == SST_PROP_BARREL_PROFILE && (type == 0 || type == 15) &&
        ((params >> 6) & 3u) >= 2;
}
bool sst_prop_valid(const SstProp *p) {
    if (!p || !stage_length(&p->scene) || !sst_prop_profile_allowed(p->key.profile, p->key.params) ||
        p->key.placed_id == 0xffff || p->key.layer != p->scene.layer ||
        (p->key.room != p->scene.room && p->key.room != 0xff) ||
        (p->phase != SST_PROP_CARRY && p->phase != SST_PROP_PLACE)) return false;
    float source_distance = 0, matrix_distance = 0;
    for (unsigned i = 0; i < 3; i++) {
        if (!bounded(p->origin[i]) || !bounded(p->pos[i]) || !bounded(p->matrix[i * 4 + 3])) return false;
        float d = p->pos[i] - p->origin[i]; source_distance += d * d;
        d = p->pos[i] - p->matrix[i * 4 + 3]; matrix_distance += d * d;
        float norm = 0;
        for (unsigned j = 0; j < 3; j++) {
            float v = p->matrix[i * 4 + j];
            if (!isfinite(v)) return false;
            norm += v * v;
        }
        if (!isfinite(norm) || fabsf(norm - 1) > 0.05f) return false;
        for (unsigned j = 0; j < i; j++) {
            float dot = 0;
            for (unsigned k = 0; k < 3; k++) dot += p->matrix[i * 4 + k] * p->matrix[j * 4 + k];
            if (fabsf(dot) > 0.05f) return false;
        }
    }
    const float *m = p->matrix;
    float det = m[0] * (m[5] * m[10] - m[6] * m[9]) -
                m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
    return isfinite(det) && fabsf(det - 1) <= 0.1f && matrix_distance <= 128.0f * 128.0f &&
        (p->phase != SST_PROP_CARRY || source_distance <= 512.0f * 512.0f);
}

// Same explicit little-endian primitives as sst_net.c's private Buf/Rd helpers.
typedef struct { uint8_t *p; size_t at; } Writer;
static void put8(Writer *w, uint8_t v) { w->p[w->at++] = v; }
static void put16(Writer *w, uint16_t v) { put8(w, (uint8_t)v); put8(w, (uint8_t)(v >> 8)); }
static void put32(Writer *w, uint32_t v) { put16(w, (uint16_t)v); put16(w, (uint16_t)(v >> 16)); }
static void putf(Writer *w, float f) { uint32_t v; memcpy(&v, &f, 4); put32(w, v); }
typedef struct { const uint8_t *p; size_t size, at; bool bad; } Reader;
static uint8_t get8(Reader *r) {
    if (r->at >= r->size) { r->bad = true; return 0; }
    return r->p[r->at++];
}
static uint16_t get16(Reader *r) { uint16_t v = get8(r); return v | (uint16_t)get8(r) << 8; }
static uint32_t get32(Reader *r) { uint32_t v = get16(r); return v | (uint32_t)get16(r) << 16; }
static float getf(Reader *r) { uint32_t v = get32(r); float f; memcpy(&f, &v, 4); return f; }
size_t sst_prop_encode(const SstProp *p, uint8_t *out, size_t capacity) {
    if (!out || !sst_prop_valid(p)) return 0;
    size_t n = stage_length(&p->scene), need = 103 + n;
    if (capacity < need) return 0;
    Writer w = {.p = out};
    put8(&w, (uint8_t)n);
    for (size_t i = 0; i < n; i++) put8(&w, (uint8_t)p->scene.stage[i]);
    put8(&w, p->scene.room); put8(&w, p->scene.layer); put8(&w, p->scene.entrance);
    put16(&w, p->key.placed_id); put16(&w, p->key.profile); put32(&w, p->key.params);
    put8(&w, p->key.room); put8(&w, p->key.layer);
    put32(&w, p->sequence); put8(&w, (uint8_t)p->phase); put32(&w, p->pose_tick);
    for (unsigned i = 0; i < 3; i++) putf(&w, p->origin[i]);
    put16(&w, (uint16_t)p->yaw);
    for (unsigned i = 0; i < 12; i++) putf(&w, p->matrix[i]);
    for (unsigned i = 0; i < 3; i++) putf(&w, p->pos[i]);
    for (unsigned i = 0; i < 3; i++) put16(&w, (uint16_t)p->rot[i]);
    return w.at;
}
bool sst_prop_decode(const uint8_t *data, size_t size, SstProp *out) {
    if (!data || !out || size < 104 || size > SST_PROP_WIRE_MAX) return false;
    Reader r = {.p = data, .size = size}; SstProp p = {0};
    unsigned n = get8(&r);
    if (!n || n > 7 || size != 103 + n) return false;
    for (unsigned i = 0; i < n; i++) {
        p.scene.stage[i] = (char)get8(&r);
        if (!p.scene.stage[i]) return false;
    }
    p.scene.room = get8(&r); p.scene.layer = get8(&r); p.scene.entrance = get8(&r);
    p.key.placed_id = get16(&r); p.key.profile = get16(&r); p.key.params = get32(&r);
    p.key.room = get8(&r); p.key.layer = get8(&r);
    p.sequence = get32(&r); p.phase = (SstPropPhase)get8(&r); p.pose_tick = get32(&r);
    for (unsigned i = 0; i < 3; i++) p.origin[i] = getf(&r);
    p.yaw = (int16_t)get16(&r);
    for (unsigned i = 0; i < 12; i++) p.matrix[i] = getf(&r);
    for (unsigned i = 0; i < 3; i++) p.pos[i] = getf(&r);
    for (unsigned i = 0; i < 3; i++) p.rot[i] = (int16_t)get16(&r);
    if (r.bad || r.at != size || !sst_prop_valid(&p)) return false;
    *out = p; return true;
}

void sst_props_reset(SstPropState *s) { if (s) memset(s, 0, sizeof *s); }
void sst_props_begin(SstPropState *s, const SstScene *scene, uint32_t generation) {
    if (!s) return;
    if (!scene || !stage_length(scene)) { sst_props_reset(s); return; }
    if (!s->initialized || s->generation != generation || !same_area(&s->scene, scene)) {
        sst_props_reset(s); s->initialized = true; s->scene = *scene; s->generation = generation;
        // Normalize ignored trailing stage bytes so exact comparison is independent of padding.
        size_t n = stage_length(scene); memset(s->scene.stage + n, 0, SST_STR - n);
    }
}
bool sst_props_roster(SstPropState *s, const SstPropPresence *live, size_t count) {
    if (!s || !s->initialized || count > SST_MAX_PLAYERS || (count && !live)) return false;
    for (size_t i = 0; i < count; i++) {
        if (!live[i].id) return false;
        for (size_t j = 0; j < i; j++) if (live[i].id == live[j].id) return false;
    }
    bool keep[SST_MAX_PLAYERS] = {0}, mapped[SST_MAX_PLAYERS] = {0};
    for (size_t i = 0; i < count; i++) for (unsigned slot = 0; slot < SST_MAX_PLAYERS; slot++) {
        SstPropPeer *peer = &s->peers[slot];
        if (peer->used && peer->active && peer->id == live[i].id && peer->generation == live[i].generation) {
            keep[slot] = true; mapped[i] = true; break;
        }
    }
    for (unsigned slot = 0; slot < SST_MAX_PLAYERS; slot++) {
        if (keep[slot]) continue;
        SstPropPeer old = s->peers[slot];
        for (unsigned prop = 0; prop < SST_PROPS; prop++) {
            SstPropTrack *track = &s->props[prop];
            track->senders[slot] = (SstPropSequence){0};
            if (old.used && track->has_target && track->target.from == old.id &&
                track->owner_generation == old.generation) track->has_target = false;
        }
        s->peers[slot] = (SstPropPeer){0};
    }
    for (size_t i = 0; i < count; i++) {
        if (mapped[i]) continue;
        for (unsigned slot = 0; slot < SST_MAX_PLAYERS; slot++) if (!s->peers[slot].used) {
            s->peers[slot] = (SstPropPeer){.used = true, .active = true,
                .id = live[i].id, .generation = live[i].generation};
            break;
        }
    }
    // Retired slots have lost their targets and sender watermarks above. A
    // targetless key with no surviving replay evidence no longer owns capacity.
    for (unsigned prop = 0; prop < SST_PROPS; prop++) {
        SstPropTrack *track = &s->props[prop];
        if (!track->used || track->has_target) continue;
        bool evidence = false;
        for (unsigned slot = 0; slot < SST_MAX_PLAYERS; slot++) {
            if (track->senders[slot].seen) { evidence = true; break; }
        }
        if (!evidence) memset(track, 0, sizeof *track);
    }
    s->roster_bound = true;
    return true;
}
static int peer_slot(SstPropState *s, uint16_t sender, uint32_t generation) {
    if (s->roster_bound) {
        for (unsigned i = 0; i < SST_MAX_PLAYERS; i++) {
            const SstPropPeer *peer = &s->peers[i];
            if (peer->used && peer->active && peer->id == sender && peer->generation == generation) return (int)i;
        }
        return -1;
    }
    int empty = -1;
    for (unsigned i = 0; i < SST_MAX_PLAYERS; i++) {
        SstPropPeer *p = &s->peers[i];
        if (!p->used) { if (empty < 0) empty = (int)i; continue; }
        if (p->id != sender) continue;
        if (p->generation == generation) return p->active ? (int)i : -1;
        if (!newer(generation, p->generation)) return -1;
        // A returning ID has a new stream; all targets/watermarks from its old incarnation end.
        for (unsigned j = 0; j < SST_PROPS; j++) {
            SstPropTrack *t = &s->props[j];
            t->senders[i] = (SstPropSequence){0};
            if (t->has_target && t->target.from == sender) t->has_target = false;
        }
        p->generation = generation; p->active = true; return (int)i;
    }
    if (empty >= 0) s->peers[empty] = (SstPropPeer){.used = true, .active = true, .id = sender, .generation = generation};
    return empty;
}
bool sst_props_update(SstPropState *s, uint16_t sender, uint32_t source_generation,
                      const SstProp *p, uint64_t now) {
    if (!s || !s->initialized || !sender || !sst_prop_valid(p) ||
        (p->received_ms && (!fresh(p->received_ms, now)))) return false;
    SstScene area = p->scene;
    size_t n = stage_length(&area); memset(area.stage + n, 0, SST_STR - n);
    if (!same_area(&s->scene, &area)) return false;
    int peer = peer_slot(s, sender, source_generation);
    if (peer < 0) return false;
    SstPropTrack *t = NULL, *empty = NULL;
    for (unsigned i = 0; i < SST_PROPS; i++) {
        if (!s->props[i].used) { if (!empty) empty = &s->props[i]; continue; }
        if (sst_prop_key_equal(&s->props[i].target.key, &p->key)) { t = &s->props[i]; break; }
    }
    if (!t) {
        if (!empty) return false; // Never evict another vase's settled placement or replay evidence.
        t = empty; t->used = true; t->target.key = p->key;
    }
    SstPropSequence *seq = &t->senders[peer];
    if (seq->seen && !newer(p->sequence, seq->sequence)) return false;
    // Keep rejected contenders' watermarks too, so an old conflicting packet cannot later win.
    *seq = (SstPropSequence){.seen = true, .sequence = p->sequence};
    if (t->has_target && t->target.from != sender) {
        if (p->phase == SST_PROP_PLACE && (t->target.phase == SST_PROP_PLACE || fresh(t->received_ms, now))) return false;
        if (t->target.phase == SST_PROP_CARRY && fresh(t->received_ms, now) && t->target.from < sender) return false;
    }
    uint64_t received = p->received_ms ? p->received_ms : now;
    t->target = *p; t->target.from = sender; t->owner_generation = source_generation;
    t->target.presence_generation = source_generation;
    t->target.received_ms = received;
    t->received_ms = received; t->has_target = true;
    return true;
}
bool sst_props_get(const SstPropState *s, const SstPropKey *key, uint64_t now, SstProp *out) {
    if (!s || !s->initialized || !key || !out) return false;
    for (unsigned i = 0; i < SST_PROPS; i++) {
        const SstPropTrack *t = &s->props[i];
        if (!t->used || !t->has_target || !sst_prop_key_equal(&t->target.key, key)) continue;
        if (t->target.phase == SST_PROP_CARRY && !fresh(t->received_ms, now)) return false;
        *out = t->target; return true;
    }
    return false;
}
void sst_props_forget(SstPropState *s, uint16_t sender, uint32_t generation) {
    if (!s || !sender) return;
    for (unsigned i = 0; i < SST_MAX_PLAYERS; i++) {
        SstPropPeer *p = &s->peers[i];
        if (!p->used || p->id != sender || p->generation != generation) continue;
        p->active = false;
        for (unsigned j = 0; j < SST_PROPS; j++)
            if (s->props[j].has_target && s->props[j].target.from == sender)
                s->props[j].has_target = false;
        return;
    }
}

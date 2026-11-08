// SPDX-License-Identifier: MIT
// Adapted from the user-provided SST handoff dated2026-10-30; original archive source retained.
#include "sst_census.h"
#include "sst_animation.h"
#include "sst_avatar.h"
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define HAND_SIZE 28672      // allocation size of every verified hand variant (sst_avatar.c)
#define PENDING 16           // unrecognised model-only draws wrist-tested per present
#define SET_LINES 10         // set-change lines per window
#define SET_WINDOW 60        // presents
#define SUMMARY_EVERY 1800   // presents (about 30 s at 60 Hz)
#define BODIES 8             // skinned palettes per present that attached pieces are tested against

typedef struct {
    SstCensusDraw d;
    uint16_t pose, label;    // fold result of a known mesh; u<label> for unrecognised entries
    bool rigid;
    char wrist;              // 0 untested, 'R' / 'L' on that verified wrist joint, '-' tested off-wrist
    float wrist_error;
    uint16_t body, joint;    // body joint it sits on (err <= 0.01) in a skinned palette of the same present
    float closest;           // smallest joint error seen (diagnostic, also when nothing matched)
    unsigned joint_presents; // presents agreeing on that body joint
    bool joint_conflict;     // seen on two different joints: never treated as attached
    unsigned offset_lines;   // per-sighting hand offset lines logged (offset_line)
    uint64_t draws, presents, last_present;
    uint16_t clip, action;   // local state at first sighting
    float frame;
} Entry;
static Entry entries[SST_CENSUS_ENTRIES];
static unsigned count, unknowns;
static uint64_t current = UINT64_MAX, window_start, drawn[SST_CENSUS_ENTRIES / 64], before[SST_CENSUS_ENTRIES / 64];
static unsigned window_lines;
static SstPose state;
static bool unknown_hand;
static struct { unsigned entry; float model[12]; } pending[PENDING];
static unsigned npending;
static struct { uint16_t mesh; uint32_t size; bool valid; float data[0x10000 / sizeof(float)]; } donor;
static struct { uint16_t entry; uint32_t size; float data[0x10000 / sizeof(float)]; } bodies[BODIES];
static unsigned nbodies;
// Parent search for model-only pieces that sit on no joint (every hand-sized piece, and every unrecognised one, e.g.
// sword and sheath parts): a child node keeps a constant transform relative to its parent joint while the body
// animates. Candidates whose relative transform changes are dropped. After a report with no candidate left, the
// search restarts (at most PARENT_ROUNDS reports per piece) so a later state, such as a drawn sword, is reported too.
#define TRACKED 12
#define PARENT_ROUNDS 4
#define TRACK_JOINTS 2048
#define PARENT_REPORT 120 // presents of agreement before a parent observation is logged
static struct {
    uint16_t entry;
    unsigned joints, presents, rounds;
    bool reported;
    struct { uint16_t body, joint; bool alive; unsigned agree; float local[12], rotation, translation; } c[TRACK_JOINTS];
} tracks[TRACKED];
static unsigned ntracks;
static SstCensusStats stats;
static FILE *output;
static int enabled = -1;

bool sst_census_enabled(void) {
    if (enabled < 0) {
        const char *e = getenv("SST_MESH_CENSUS");
        enabled = e && !strcmp(e, "1");
    }
    return enabled;
}

static void emit(const char *fmt, ...) {
    if (stats.lines >= SST_CENSUS_LINES) { stats.suppressed++; return; }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(output ? output : stderr, fmt, ap);
    va_end(ap);
    stats.lines++;
}

static void name(const Entry *e, char *out, size_t cap) {
    if (e->d.known) snprintf(out, cap, "k%u", e->d.known);
    else snprintf(out, cap, "u%u", e->label);
}

static float error(const float *a, const float *b) {
    float worst = 0;
    for (unsigned k = 0; k < 12; k++) {
        if (!isfinite(a[k]) || !isfinite(b[k])) return INFINITY;
        worst = fmaxf(worst, fabsf(a[k] - b[k]));
    }
    return worst;
}

// Is this model on the verified wrist joint of side R (pose mesh 23) or L (34), in this present's donor body?
static char wrist_side(const float model[12], float *best) {
    static const struct { uint16_t mesh; char side; } sides[] = {{23, 'R'}, {34, 'L'}};
    char found = 0;
    *best = INFINITY;
    for (unsigned i = 0; i < 2; i++) {
        uint16_t body, joint;
        uint32_t size;
        if (!sst_animation_wrist_link(sides[i].mesh, &body, &joint, &size) || !donor.valid || donor.mesh != body ||
            donor.size != size || (joint + 1u) * 48 > size) continue;
        float e = error(model, donor.data + joint * 12);
        if (e < *best) *best = e;
        if (e <= .01f) found = sides[i].side;
    }
    return found ? found : isfinite(*best) ? '-' : 0;
}

// General 3x4 inverse (bone matrices may carry scale).
static bool invert(const float m[12], float out[12]) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g, det = a * A + b * B + c * C;
    if (!isfinite(det) || fabsf(det) < 1e-8f) return false;
    float r[9] = {A / det, (c * h - b * i) / det, (b * f - c * e) / det,
                  B / det, (a * i - c * g) / det, (c * d - a * f) / det,
                  C / det, (b * g - a * h) / det, (a * e - b * d) / det};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) out[row * 4 + col] = r[row * 3 + col];
        out[row * 4 + 3] = -(r[row * 3] * m[3] + r[row * 3 + 1] * m[7] + r[row * 3 + 2] * m[11]);
    }
    return true;
}
static void multiply(const float a[12], const float b[12], float out[12]) {
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 4; col++)
            out[row * 4 + col] = a[row * 4] * b[col] + a[row * 4 + 1] * b[4 + col] + a[row * 4 + 2] * b[8 + col] +
                                 (col == 3 ? a[row * 4 + 3] : 0);
}
static bool same_local(const float a[12], const float b[12], float *rotation, float *translation) {
    bool same = true;
    for (int k = 0; k < 12; k++) {
        float d = isfinite(a[k]) && isfinite(b[k]) ? fabsf(a[k] - b[k]) : INFINITY;
        float *worst = k % 4 == 3 ? translation : rotation;
        if (d > *worst) *worst = d;
        if (d > (k % 4 == 3 ? .5f : .02f)) same = false;
    }
    return same;
}
static void track_parent(unsigned entry, const float model[12]) {
    unsigned t = 0;
    while (t < ntracks && tracks[t].entry != entry) t++;
    if (t == ntracks) {
        if (ntracks == TRACKED || !nbodies) return;
        ntracks++;
        tracks[t].entry = (uint16_t)entry;
        tracks[t].joints = tracks[t].presents = tracks[t].rounds = 0;
        tracks[t].reported = false;
    }
    if (!tracks[t].joints) {
        // (Re)seed the candidates from this present's palettes.
        if (!nbodies) return;
        tracks[t].presents = 0;
        tracks[t].reported = false;
        for (unsigned b = 0; b < nbodies; b++)
            for (unsigned j = 0; j < bodies[b].size / 48 && tracks[t].joints < TRACK_JOINTS; j++) {
                float inv[12];
                if (!invert(bodies[b].data + j * 12, inv)) continue;
                unsigned k = tracks[t].joints++;
                tracks[t].c[k].body = bodies[b].entry;
                tracks[t].c[k].joint = (uint16_t)j;
                tracks[t].c[k].alive = true;
                tracks[t].c[k].agree = 0;
                tracks[t].c[k].rotation = tracks[t].c[k].translation = 0;
                multiply(inv, model, tracks[t].c[k].local);
            }
        return;
    }
    tracks[t].presents++;
    unsigned alive = 0;
    for (unsigned k = 0; k < tracks[t].joints; k++) {
        for (unsigned b = 0; b < nbodies; b++) {
            if (bodies[b].entry != tracks[t].c[k].body || (tracks[t].c[k].joint + 1u) * 48 > bodies[b].size) continue;
            float inv[12], local[12];
            if (!invert(bodies[b].data + tracks[t].c[k].joint * 12, inv)) break;
            multiply(inv, model, local);
            if (same_local(local, tracks[t].c[k].local, &tracks[t].c[k].rotation, &tracks[t].c[k].translation)) {
                if (tracks[t].c[k].alive) tracks[t].c[k].agree++;
            } else tracks[t].c[k].alive = false;
            break;
        }
        if (tracks[t].c[k].alive) alive++;
    }
    if (!tracks[t].reported && tracks[t].presents >= PARENT_REPORT && alive <= 4) {
        tracks[t].reported = true;
        char n[16], line[400];
        name(&entries[entry], n, sizeof n);
        int at = snprintf(line, sizeof line, "[sst-mesh] parent %s after %u presents:", n, tracks[t].presents);
        for (unsigned k = 0; k < tracks[t].joints && at < (int)sizeof line - 60; k++) {
            if (!tracks[t].c[k].alive || tracks[t].c[k].agree < PARENT_REPORT / 2) continue;
            char bn[16];
            name(&entries[tracks[t].c[k].body], bn, sizeof bn);
            const float *l = tracks[t].c[k].local;
            at += snprintf(line + at, sizeof line - at, " %s joint %u (agree %u, offset %.1f %.1f %.1f)", bn,
                           tracks[t].c[k].joint, tracks[t].c[k].agree, l[3], l[7], l[11]);
        }
        emit("%s (an observation, not a mapping)\n", line);
        // The three most nearly constant candidates, also when none stayed within tolerance.
        bool shown[TRACK_JOINTS] = {0};
        for (unsigned pick = 0; pick < 3; pick++) {
            int best = -1;
            float best_score = INFINITY;
            for (unsigned k = 0; k < tracks[t].joints; k++) {
                float score = tracks[t].c[k].rotation + tracks[t].c[k].translation / 100;
                if (!shown[k] && score < best_score) best = (int)k, best_score = score;
            }
            if (best < 0) break;
            shown[best] = true;
            char bn[16];
            name(&entries[tracks[t].c[best].body], bn, sizeof bn);
            const float *l = tracks[t].c[best].local;
            emit("[sst-mesh]   nearest %s joint %u: max rotation dev %.4f, max translation dev %.2f, offset %.2f %.2f %.2f"
                 " clip=%u act=%u\n", bn, tracks[t].c[best].joint, tracks[t].c[best].rotation,
                 tracks[t].c[best].translation, l[3], l[7], l[11], state.anim_id, state.action);
        }
    }
    // Reported and no candidate left (rotating child, or the piece moved to another parent such as a drawn sword):
    // search again so the next state gets its own report.
    if (tracks[t].reported && !alive && tracks[t].rounds + 1 < PARENT_ROUNDS) tracks[t].rounds++, tracks[t].joints = 0;
}

// Per-sighting origin of a hand-sized model-only piece relative to the hand joints 3 and 5 of a 368-joint palette
// (sst_avatar_hand_parent). Transient variants (one present per sighting) never reach the parent search; equal
// offsets to a recognised hand's on the same joint, sighting after sighting, are the evidence that they replace it.
#define OFFSET_LINES_UNKNOWN 8
#define OFFSET_LINES_KNOWN 2
static void offset_line(Entry *e, const float model[12]) {
    if (!e->rigid || e->d.size != HAND_SIZE || e->offset_lines >= (e->d.known ? OFFSET_LINES_KNOWN : OFFSET_LINES_UNKNOWN))
        return;
    unsigned b = 0;
    while (b < nbodies && bodies[b].size != 368 * 48) b++;
    if (b == nbodies) return;
    float o[2][3];
    for (unsigned i = 0; i < 2; i++) {
        float inv[12], local[12];
        if (!invert(bodies[b].data + (i ? 5 : 3) * 12, inv)) return;
        multiply(inv, model, local);
        o[i][0] = local[3], o[i][1] = local[7], o[i][2] = local[11];
    }
    e->offset_lines++;
    char n[16], bn[16];
    name(e, n, sizeof n);
    name(&entries[bodies[b].entry], bn, sizeof bn);
    emit("[sst-mesh] offset %s %s joint3 %.2f %.2f %.2f joint5 %.2f %.2f %.2f clip=%u act=%u fr=%.1f present=%llu "
         "(an observation, not a mapping)\n", n, bn, o[0][0], o[0][1], o[0][2], o[1][0], o[1][1], o[1][2], state.anim_id,
         state.action, state.anim_frame, (unsigned long long)current);
}
static void end_present(void) {
    if (current == UINT64_MAX) return;
    if (unknown_hand) stats.unknown_hand_presents++;
    for (unsigned i = 0; i < npending; i++) offset_line(&entries[pending[i].entry], pending[i].model);
    for (unsigned i = 0; i < npending; i++) {
        Entry *e = &entries[pending[i].entry];
        if (e->joint_conflict) continue;
        unsigned best_joint = 0, best_body = 0;
        float best = INFINITY;
        for (unsigned b = 0; b < nbodies; b++)
            for (unsigned j = 0; j < bodies[b].size / 48; j++) {
                float err = error(pending[i].model, bodies[b].data + j * 12);
                if (err < best) best = err, best_joint = j, best_body = b;
            }
        if (best < e->closest) e->closest = best;
        if (best > .01f) {
            if (e->rigid && (e->d.size == HAND_SIZE || !e->d.known)) track_parent(pending[i].entry, pending[i].model);
            continue;
        }
        char n[16], bn[16];
        name(e, n, sizeof n);
        name(&entries[bodies[best_body].entry], bn, sizeof bn);
        uint16_t body = bodies[best_body].entry;
        if (!e->joint_presents) {
            e->body = body, e->joint = (uint16_t)best_joint, e->joint_presents = 1;
            emit("[sst-mesh] joint %s body=%s joint=%u err=%.4f clip=%u act=%u (an observation, not a mapping)\n", n, bn,
                 best_joint, best, state.anim_id, state.action);
        } else if (e->body == body && e->joint == best_joint) e->joint_presents++;
        else {
            e->joint_conflict = true;
            emit("[sst-mesh] joint %s conflict: joint %u then %s joint %u\n", n, e->joint, bn, best_joint);
        }
    }
    for (unsigned i = 0; i < npending; i++) {
        Entry *e = &entries[pending[i].entry];
        if (e->d.known || e->wrist == '?') continue; // wrist tests are for unrecognised pieces; '?' stays unclassified
        float err;
        char side = wrist_side(pending[i].model, &err);
        if (!side || side == e->wrist) continue;
        if (e->wrist == 'R' || e->wrist == 'L') side = '?'; // seen on and off a wrist: never classify
        e->wrist = side;
        e->wrist_error = err;
        char n[16];
        name(e, n, sizeof n);
        emit("[sst-mesh] wrist %s side=%c err=%.4f clip=%u act=%u (an observation, not a mapping)\n", n, side, err,
             state.anim_id, state.action);
    }
    if (memcmp(drawn, before, sizeof drawn) && current > 0) {
        if (current >= window_start + SET_WINDOW) window_start = current, window_lines = 0;
        if (window_lines < SET_LINES) {
            char line[512];
            int at = snprintf(line, sizeof line, "[sst-mesh] set present=%llu clip=%u act=%u fr=%.1f eq=0x%02x",
                              (unsigned long long)current, state.anim_id, state.action, state.anim_frame, state.equip_flags);
            for (unsigned i = 0; i < count && at < (int)sizeof line - 16; i++) {
                bool now = drawn[i / 64] >> (i % 64) & 1, was = before[i / 64] >> (i % 64) & 1;
                if (now == was) continue;
                char n[16];
                name(&entries[i], n, sizeof n);
                at += snprintf(line + at, sizeof line - at, " %c%s", now ? '+' : '-', n);
            }
            emit("%s\n", line);
            window_lines++;
        } else stats.suppressed++;
    }
    memcpy(before, drawn, sizeof drawn);
    if (stats.presents && stats.presents % SUMMARY_EVERY == 0) {
        SstCensusStats s = sst_census_stats();
        emit("[sst-mesh] summary presents=%llu entries=%u unknown=%u unknown_rigid_near=%u on_wrist=%u unknown_hand_presents=%llu\n",
             (unsigned long long)s.presents, s.entries, s.unknown_entries, s.unknown_rigid_near, s.unknown_on_wrist,
             (unsigned long long)s.unknown_hand_presents);
        for (unsigned i = 0; i < count; i++) {
            const Entry *e = &entries[i];
            if (e->d.known || !e->rigid) continue;
            emit("[sst-mesh]   u%u hash=%016llx size=%u draws=%llu presents=%llu wrist=%c joint=%s%u/%u presents closest=%.4f\n",
                 e->label, (unsigned long long)e->d.hash, e->d.size, (unsigned long long)e->draws,
                 (unsigned long long)e->presents, e->wrist ? e->wrist : '.', e->joint_conflict ? "conflict " : "",
                 e->joint, e->joint_presents, e->closest);
        }
    }
}

void sst_census_draw(const SstCensusDraw *d, const SstPose *local, const float view[12], const float model[12],
                     const void *positions, uint64_t present) {
    if (!d || !local || !view || !model || (d->positions && !positions) ||
        d->positions > sizeof donor.data || d->normals > sizeof donor.data ||
        d->positions % 48 || d->normals % 48) return;
    if (current != UINT64_MAX && present < current) sst_census_reset(output);
    if (present != current) {
        end_present();
        current = present;
        state = *local;
        memset(drawn, 0, sizeof drawn);
        npending = 0;
        donor.valid = false;
        nbodies = 0;
        unknown_hand = false;
        stats.presents++;
    }
    if (!sst_avatar_local_skin(local, view, model, positions, d->positions)) return; // not this Link's geometry
    Entry *e = NULL;
    unsigned index = 0;
    for (; index < count; index++) {
        const SstCensusDraw *k = &entries[index].d;
        if (k->hash == d->hash && k->size == d->size && k->positions == d->positions && k->normals == d->normals &&
            k->count == d->count && k->first == d->first) { e = &entries[index]; break; }
    }
    if (!e) {
        if (count == SST_CENSUS_ENTRIES) { stats.suppressed++; return; }
        index = count++;
        e = &entries[index];
        *e = (Entry){.d = *d, .rigid = !d->positions && !d->normals, .clip = local->anim_id, .action = local->action,
                     .frame = local->anim_frame, .closest = INFINITY};
        e->pose = d->known ? sst_avatar_pose_mesh(d->known, d->positions, d->normals) : 0;
        if (!d->known) e->label = (uint16_t)++unknowns;
        char n[16];
        name(e, n, sizeof n);
        emit("[sst-mesh] new %s hash=%016llx size=%u kind=%s bones=%u count=%u first=%u pose=%u clip=%u act=%u fr=%.1f "
             "eq=0x%02x present=%llu\n", n, (unsigned long long)d->hash, d->size, e->rigid ? "rigid" : "skinned",
             d->positions / 48, d->count, d->first, e->pose, local->anim_id, local->action, local->anim_frame,
             local->equip_flags, (unsigned long long)present);
    }
    e->draws++;
    if (e->last_present != present) e->presents++, e->last_present = present;
    drawn[index / 64] |= 1ull << (index % 64);
    if (d->positions && nbodies < BODIES) {
        // Skinned palettes of this present (recognised or not): the joints attached pieces are tested against.
        bool have = false;
        for (unsigned b = 0; b < nbodies; b++) have |= bodies[b].entry == index;
        if (!have) {
            memcpy(bodies[nbodies].data, positions, d->positions);
            bodies[nbodies].entry = (uint16_t)index;
            bodies[nbodies].size = d->positions;
            nbodies++;
        }
    }
    if (d->known && d->positions && !donor.valid) {
        // Copy the palette of a verified wrist donor body once per present, for the wrist test.
        for (uint16_t side = 23; side <= 34; side += 11) {
            uint16_t body, joint;
            uint32_t size;
            if (sst_animation_wrist_link(side, &body, &joint, &size) && body == d->known && size == d->positions) {
                memcpy(donor.data, positions, size);
                donor.mesh = body;
                donor.size = size;
                donor.valid = true;
                break;
            }
        }
    }
    if (e->rigid && (!d->known || d->size == HAND_SIZE)) {
        if (!d->known && d->size == HAND_SIZE) unknown_hand = true;
        if (npending < PENDING) {
            pending[npending].entry = index;
            memcpy(pending[npending].model, model, 48);
            npending++;
        }
    }
}

SstCensusStats sst_census_stats(void) {
    SstCensusStats s = stats;
    s.entries = count;
    for (unsigned i = 0; i < count; i++) {
        const Entry *e = &entries[i];
        if (e->d.known) continue;
        s.unknown_entries++;
        if (e->rigid) s.unknown_rigid_near++;
        if (e->wrist == 'R' || e->wrist == 'L') s.unknown_on_wrist++;
    }
    return s;
}

void sst_census_reset(FILE *out) {
    memset(entries, 0, sizeof entries);
    count = unknowns = 0;
    current = UINT64_MAX;
    window_start = 0;
    window_lines = 0;
    memset(drawn, 0, sizeof drawn);
    memset(before, 0, sizeof before);
    npending = 0;
    donor.valid = false;
    nbodies = ntracks = 0;
    unknown_hand = false;
    memset(&stats, 0, sizeof stats);
    output = out;
}

// Enemy ownership, follower smoothing and health/defeat reconciliation. Pure: no game, no network.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CHECK(condition) assert(condition)
#define RUN(function) do { function(); } while (0)
#define TEST_EXIT() do { puts("archive enemy smoothing/ownership/health invariants PASS"); return 0; } while (0)
#include "../src/sst_enemy_motion.h"
#include "../src/sst_world.h"
#include <math.h>

static const SstScene area = {"D100", 2, 0, 0};
#define PROFILE 0x16d
#define KEY ((uint64_t)7 << 32 | (uint64_t)PROFILE << 16 | 42)
#define INSTANCE 9001u
static SstMotionState ms;

static SstEnemyMotion packet(uint16_t from, uint32_t seq, uint64_t arrival, float x, int16_t yaw) {
    SstEnemyMotion m = {.scene = area, .key = KEY, .sequence = seq, .profile = PROFILE, .from = from,
                        .received_ms = arrival, .presence_generation = 1};
    m.pos[0] = x; m.pos[1] = 10; m.pos[2] = -5; m.rot[1] = yaw;
    return m;
}
static void fresh_state(uint16_t owner, uint64_t now) {
    memset(&ms, 0, sizeof ms);
    sst_motion_begin_at(&ms, &area, 1, owner, now);
    sst_motion_observe(&ms, KEY, INSTANCE, PROFILE);
}
static bool sample(uint64_t now, float *x, int16_t *yaw) {
    float p[3], v[3]; int16_t r[3];
    if (!sst_motion_sample(&ms, KEY, INSTANCE, now, p, r, v)) return false;
    if (x) *x = p[0];
    if (yaw) *yaw = r[1];
    return true;
}
// Owner moves at 0.5 units/ms. Packets every `spacing` ms (optional jitter, optional drops);
// the follower samples every 16 ms. Returns the longest run (ms) without displayed movement.
static unsigned longest_hold(unsigned spacing, int jitter, const unsigned *drops, unsigned ndrops,
                             float *max_overshoot, float *max_step) {
    fresh_state(2, 1000);
    uint64_t next = 1000; uint32_t seq = 0; unsigned k = 0;
    float last = NAN; uint64_t still_since = 0; unsigned hold = 0;
    *max_overshoot = 0; *max_step = 0;
    for (uint64_t t = 1000; t < 4000; t += 4) {
        if (t >= next) {
            bool dropped = false;
            for (unsigned d = 0; d < ndrops; d++) dropped |= drops[d] == k;
            float x = 0.5f * (float)(next - 1000);
            SstEnemyMotion m = packet(2, ++seq, t, x, 0);
            if (!dropped) sst_motion_receive(&ms, &m, t);
            k++;
            int j = jitter ? (int)((k * 7919u) % (unsigned)(2 * jitter + 1)) - jitter : 0;
            next = 1000 + (uint64_t)k * spacing + (uint64_t)(int64_t)j;
        }
        if ((t - 1000) % 16 || t < 1300) continue;
        float x;
        if (!sample(t, &x, NULL)) { hold = 9999; break; }
        // The owner's real position is 0.5*(t-1000); the follower may lag but must not run ahead
        // by more than the 150 ms extrapolation bound.
        float real = 0.5f * (float)(t - 1000);
        if (x - real > *max_overshoot) *max_overshoot = x - real;
        if (!isnan(last)) {
            if (fabsf(x - last) > *max_step) *max_step = fabsf(x - last);
            if (fabsf(x - last) < 1e-4f) { if (!still_since) still_since = t - 16; }
            else still_since = 0;
            if (still_since && t - still_since > hold) hold = (unsigned)(t - still_since);
        }
        last = x;
    }
    return hold;
}
static void interpolation_at_133ms_never_holds(void) {
    float over, step;
    unsigned hold = longest_hold(133, 0, NULL, 0, &over, &step);
    fprintf(stderr, "  133 ms: longest hold %u ms, overshoot %.1f, max step %.1f\n", hold, over, step);
    CHECK(hold < 30);
    CHECK(step < 0.5f * 16 * 2.0f); // never more than twice the true per-frame motion
    hold = longest_hold(133, 25, NULL, 0, &over, &step);
    fprintf(stderr, "  133+-25 ms: longest hold %u ms, overshoot %.1f, max step %.1f\n", hold, over, step);
    CHECK(hold < 30);
    hold = longest_hold(100, 0, NULL, 0, &over, &step);
    CHECK(hold < 30);
    hold = longest_hold(200, 0, NULL, 0, &over, &step);
    CHECK(hold < 30);
}
static void extrapolation_is_bounded_after_drops(void) {
    float over, step;
    unsigned one[] = {10};
    unsigned hold = longest_hold(133, 0, one, 1, &over, &step);
    fprintf(stderr, "  1 drop: hold %u ms, overshoot %.1f, max step %.1f\n", hold, over, step);
    CHECK(hold < 30);                       // 150 ms of dead reckoning covers one missing packet
    CHECK(over <= 0.5f * SST_MOTION_EXTRAP_MS + 1);
    CHECK(step < 0.5f * 16 * 2.5f);         // correction stays smooth
    unsigned two[] = {10, 11};
    hold = longest_hold(133, 0, two, 2, &over, &step);
    fprintf(stderr, "  2 drops: hold %u ms, overshoot %.1f, max step %.1f\n", hold, over, step);
    CHECK(hold < 200);                      // bounded: holds after 150 ms instead of running away
    CHECK(over <= 0.5f * SST_MOTION_EXTRAP_MS + 1);
    CHECK(step < 0.5f * 16 * 3.0f);
    // Explicit bound: one sample pair, then silence. Displacement stops at rate * 150 ms.
    fresh_state(2, 0);
    SstEnemyMotion a = packet(2, 1, 1000, 0, 0), b = packet(2, 2, 1133, 66.5f, 0);
    CHECK(sst_motion_receive(&ms, &a, 1000)); CHECK(sst_motion_receive(&ms, &b, 1133));
    float x0, x1, x2;
    CHECK(sample(1133 + 133 + SST_MOTION_EXTRAP_MS, &x0, NULL));
    CHECK(sample(1133 + 133 + SST_MOTION_EXTRAP_MS + 100, &x1, NULL));
    CHECK(fabsf(x0 - x1) < 1e-3f);
    CHECK(x0 <= 66.5f + 0.5f * SST_MOTION_EXTRAP_MS + 0.5f);
    // The 500 ms timeout still returns the actor to native AI.
    CHECK(!sample(1133 + SST_MOTION_TIMEOUT_MS + 1, &x2, NULL));
}
static void rotation_takes_the_shortest_arc(void) {
    fresh_state(2, 0);
    SstEnemyMotion a = packet(2, 1, 1000, 0, 0x7f00), b = packet(2, 2, 1133, 0, (int16_t)0x8100);
    CHECK(sst_motion_receive(&ms, &a, 1000)); CHECK(sst_motion_receive(&ms, &b, 1133));
    int16_t mid;
    CHECK(sample(1133 + 66, NULL, &mid));
    // Halfway between 0x7f00 and 0x8100 across +-180 degrees is near 0x8000, never near 0.
    CHECK((uint16_t)mid >= 0x7f00 && (uint16_t)mid <= 0x8100);
    int16_t end; CHECK(sample(1133 + 400, NULL, &end)); CHECK(end == (int16_t)0x8100);
}
static void sequence_and_generation_filter(void) {
    fresh_state(2, 0);
    SstEnemyMotion a = packet(2, 5, 1000, 0, 0), dup = packet(2, 5, 1010, 50, 0), old = packet(2, 4, 1020, 60, 0);
    CHECK(sst_motion_receive(&ms, &a, 1000));
    CHECK(!sst_motion_receive(&ms, &dup, 1010));
    CHECK(!sst_motion_receive(&ms, &old, 1020));
    SstEnemyMotion rejoin = packet(2, 1, 1030, 70, 0); rejoin.presence_generation = 2;
    CHECK(sst_motion_receive(&ms, &rejoin, 1030)); // a reconnect restarts its sequence
    SstEnemyMotion stale_gen = packet(2, 99, 1040, 80, 0);
    CHECK(!sst_motion_receive(&ms, &stale_gen, 1040));
    SstEnemyMotion other_area = packet(2, 2, 1050, 0, 0); other_area.scene.room = 3;
    CHECK(!sst_motion_receive(&ms, &other_area, 1050));
    SstEnemyMotion stranger = packet(3, 1, 1060, 0, 0);
    CHECK(!sst_motion_receive(&ms, &stranger, 1060));
    CHECK(ms.rejected >= 5);
}
static void owner_change_keeps_tracks_continuous(void) {
    fresh_state(2, 0);
    for (uint32_t i = 0; i < 5; i++) {
        SstEnemyMotion m = packet(2, i + 1, 1000 + i * 133, 0.5f * i * 133, 0);
        CHECK(sst_motion_receive(&ms, &m, 1000 + i * 133));
    }
    float before, after;
    CHECK(sample(1600, &before, NULL));
    sst_motion_begin_at(&ms, &area, 1, 3, 1600); // player 2 opened a menu: 3 now owns the area
    CHECK(ms.owner_changes == 1);
    CHECK(sample(1600, &after, NULL));
    CHECK(fabsf(before - after) < 1e-3f);       // no wipe, no snap
    // Player 2's in-flight packet is still usable for its own track during the grace...
    SstEnemyMotion late = packet(2, 6, 1665, 0.5f * 665, 0);
    CHECK(sst_motion_receive(&ms, &late, 1666));
    // ...until the new owner's first packet, which blends from the displayed sample.
    float shown; CHECK(sample(1700, &shown, NULL));
    SstEnemyMotion first = packet(3, 1, 1700, 0.5f * 700 + 3, 0); first.presence_generation = 4;
    CHECK(sst_motion_receive(&ms, &first, 1700));
    float now_x; CHECK(sample(1700, &now_x, NULL));
    CHECK(fabsf(now_x - shown) < 1e-3f);
    SstEnemyMotion later = packet(2, 7, 1720, 0.5f * 720, 0);
    CHECK(!sst_motion_receive(&ms, &later, 1720)); // the new owner has taken this track over
    // After the grace the previous owner is ignored everywhere.
    sst_motion_observe(&ms, KEY + 1, INSTANCE + 1, PROFILE);
    SstEnemyMotion other = packet(2, 8, 1600 + SST_MOTION_HANDOFF_MS + 10, 0, 0); other.key = KEY + 1;
    CHECK(!sst_motion_receive(&ms, &other, 1600 + SST_MOTION_HANDOFF_MS + 10));
    // A different load or area still wipes everything.
    sst_motion_begin_at(&ms, &area, 2, 3, 1800);
    CHECK(!sample(1800, &now_x, NULL));
}
static void first_sample_blends_from_native_transform(void) {
    fresh_state(2, 0);
    float here[3] = {100, 10, -5}; int16_t facing[3] = {0, 0x1000, 0};
    sst_motion_observe_at(&ms, KEY, INSTANCE, PROFILE, here, facing);
    SstEnemyMotion m = packet(2, 1, 1000, 160, 0x2000);
    CHECK(sst_motion_receive(&ms, &m, 1000));
    float x; int16_t yaw;
    CHECK(sample(1000, &x, &yaw)); CHECK(fabsf(x - 100) < 1e-3f); CHECK(yaw == 0x1000);
    CHECK(sample(1000 + 400, &x, &yaw)); CHECK(fabsf(x - 160) < 1e-3f); CHECK(yaw == 0x2000);
    // A teleport beyond the snap distance does not slide across the room.
    fresh_state(2, 0);
    sst_motion_observe_at(&ms, KEY, INSTANCE, PROFILE, here, facing);
    SstEnemyMotion jump = packet(2, 1, 1000, 100 + SST_MOTION_SNAP_DISTANCE + 50, 0);
    CHECK(sst_motion_receive(&ms, &jump, 1000));
    CHECK(sample(1000, &x, NULL)); CHECK(fabsf(x - jump.pos[0]) < 1e-3f);
}
static SstPlayer peer(uint16_t id, const char *team, uint64_t pose_ms, uint8_t flags, uint16_t hp) {
    SstPlayer p = {.active = true, .id = id, .has_pose = true, .scene = area, .pose_time_ms = pose_ms};
    snprintf(p.team, sizeof p.team, "%s", team);
    p.pose.equip_flags = flags; p.pose.health = hp; p.pose.max_health = 12;
    return p;
}
static void owner_agreement_with_mixed_teams(void) {
    uint64_t now = 10000;
    // Four players: 1 is in another team, 2 is in a menu, 3 and 4 are ordinary teammates.
    // Equipment bits 0-4 of the pose flags must not affect ownership.
    SstPlayer all[4] = {peer(1, "blue", now - 10, 0x20, 12), peer(2, "red", now - 10, 0x60, 12),
                        peer(3, "red", now - 10, 0x20 | 0x1f, 12), peer(4, "red", now - 10, 0x20, 12)};
    SstMotionCandidate host[4];
    for (int i = 0; i < 4; i++)
        host[i] = (SstMotionCandidate){all[i].id, all[i].team, &all[i].scene, all[i].pose_time_ms,
                                       all[i].pose.equip_flags, all[i].pose.health};
    CHECK(sst_motion_pick_owner(host, 4, "red", &area, now) == 3);
    CHECK(sst_motion_pick_owner(host, 4, "blue", &area, now) == 1);
    // Every red client computes the same owner from its own view (itself excluded from the list).
    for (int me = 1; me < 4; me++) {
        SstPlayer others[3]; int n = 0;
        for (int i = 0; i < 4; i++) if (i != me) others[n++] = all[i];
        bool eligible = (all[me].pose.equip_flags & 0xe0) == 0x20;
        CHECK(sst_motion_owner(all[me].id, eligible, &area, "red", others, n, now) == 3);
    }
    // Stale, dead, boss and other-area candidates are skipped; nobody eligible means no owner.
    SstPlayer odd[4] = {peer(1, "red", now - SST_MOTION_FRESH_MS - 1, 0x20, 12), peer(2, "red", now, 0x20, 0),
                        peer(3, "red", now, 0xa0, 12), peer(4, "red", now, 0x20, 12)};
    odd[3].scene.layer = 1;
    CHECK(sst_motion_owner(9, false, &area, "red", odd, 4, now) == 0);
    CHECK(sst_motion_owner(9, true, &area, "red", odd, 4, now) == 9);
    CHECK(sst_motion_owner(0, true, &area, "red", odd, 4, now) == 0); // offline id never owns
}

// ---- health and defeat reconciliation (TCP T_ENEMY path)
static SstEnemies owner_side, follower_side;
static SstEnemyEvent outbox[16]; static int out_n;
static void collect(const SstEnemyEvent *e, void *u) { (void)u; if (out_n < 16) outbox[out_n++] = *e; }
static SstEnemyEvent from_peer(SstEnemyEvent e, uint16_t from) { e.from = from; e.presence_generation = 1; return e; }
static void setup_pair(uint16_t hp) {
    memset(&owner_side, 0, sizeof owner_side); memset(&follower_side, 0, sizeof follower_side);
    sst_enemies_begin(&owner_side, &area, 1); sst_enemies_begin(&follower_side, &area, 1);
    out_n = 0;
    sst_enemies_observe_live(&owner_side, KEY, INSTANCE, PROFILE, hp, collect, NULL);
    sst_enemies_observe_live(&follower_side, KEY, INSTANCE + 5, PROFILE, hp, collect, NULL);
    out_n = 0;
}
static void follower_hit_reaches_owner(void) {
    setup_pair(10);
    sst_enemies_begin(&follower_side, &area, 1);
    sst_enemies_observe_live(&follower_side, KEY, INSTANCE + 5, PROFILE, 7, collect, NULL); // local sword hit
    CHECK(out_n == 1 && outbox[0].damage == 3 && outbox[0].health == 7 && !outbox[0].snapshot);
    sst_enemies_begin(&owner_side, &area, 1);
    sst_enemies_observe_live(&owner_side, KEY, INSTANCE, PROFILE, 10, collect, NULL);
    SstEnemyEvent anonymous = outbox[0]; // no sender id: ignored
    SstEnemyTrack *t = sst_enemies_receive(&owner_side, &anonymous);
    CHECK(!t);
    SstEnemyEvent e = from_peer(outbox[0], 2);
    t = sst_enemies_receive(&owner_side, &e);
    CHECK(t && t->health == 7);
    CHECK(!sst_enemies_receive(&owner_side, &e)); // duplicate delivery is ignored
}
static void concurrent_hits_converge(void) {
    setup_pair(10);
    sst_enemies_begin(&owner_side, &area, 1); sst_enemies_begin(&follower_side, &area, 1);
    sst_enemies_observe_live(&owner_side, KEY, INSTANCE, PROFILE, 6, collect, NULL);        // owner hits 4
    sst_enemies_observe_live(&follower_side, KEY, INSTANCE + 5, PROFILE, 7, collect, NULL); // follower hits 3
    CHECK(out_n == 2);
    SstEnemyEvent to_follower = from_peer(outbox[0], 1), to_owner = from_peer(outbox[1], 2);
    SstEnemyTrack *a = sst_enemies_receive(&follower_side, &to_follower);
    SstEnemyTrack *b = sst_enemies_receive(&owner_side, &to_owner);
    CHECK(a && b && a->health == 3 && b->health == 3);
}
static void defeat_wins_and_duplicates_are_ignored(void) {
    setup_pair(10);
    sst_enemies_begin(&owner_side, &area, 1);
    sst_enemies_observe_live(&owner_side, KEY, INSTANCE, PROFILE, 0, collect, NULL); // owner defeats it
    CHECK(out_n == 1 && outbox[0].health == 0);
    SstEnemyEvent death = from_peer(outbox[0], 1);
    SstEnemyTrack *t = sst_enemies_receive(&follower_side, &death);
    CHECK(t && t->health == 0);
    CHECK(!sst_enemies_receive(&follower_side, &death));               // same event replayed
    SstEnemyEvent second = death; second.from = 3; second.sequence = 77; // another peer also saw it die
    CHECK(!sst_enemies_receive(&follower_side, &second));
    // A stale, higher-health snapshot (e.g. a late spawn probe) cannot resurrect it; the
    // follower answers with the retained defeat instead.
    out_n = 0;
    SstEnemyEvent stale = {.scene = area, .key = KEY, .sequence = 5, .profile = PROFILE,
                           .health = 10, .snapshot = true, .from = 4, .presence_generation = 1};
    CHECK(!sst_enemies_exchange(&follower_side, &stale, collect, NULL));
    CHECK(out_n == 1 && outbox[0].snapshot && outbox[0].health == 0);
    // A stale damage event that reports more health than the defeat cannot raise it either.
    SstEnemyEvent late_hit = {.scene = area, .key = KEY, .sequence = 6, .profile = PROFILE,
                              .damage = 1, .health = 9, .from = 4, .presence_generation = 1};
    CHECK(!sst_enemies_receive(&follower_side, &late_hit));
    bool still_dead = false;
    for (int i = 0; i < SST_ENEMIES; i++)
        if (follower_side.actors[i].used && follower_side.actors[i].key == KEY) still_dead = !follower_side.actors[i].health;
    CHECK(still_dead);
}

int main(void) {
    RUN(interpolation_at_133ms_never_holds);
    RUN(extrapolation_is_bounded_after_drops);
    RUN(rotation_takes_the_shortest_arc);
    RUN(sequence_and_generation_filter);
    RUN(owner_change_keeps_tracks_continuous);
    RUN(first_sample_blends_from_native_transform);
    RUN(owner_agreement_with_mixed_teams);
    RUN(follower_hit_reaches_owner);
    RUN(concurrent_hits_converge);
    RUN(defeat_wins_and_duplicates_are_ignored);
    TEST_EXIT();
}

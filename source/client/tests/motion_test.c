#include "../src/sst_enemy_motion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    SstScene area = {.stage = "D000"};
    SstPlayer ps[3] = {0};
    for (int i = 0; i < 3; i++) {
        ps[i] = (SstPlayer){.active = true, .id = i + 1, .has_pose = true, .scene = area,
            .pose_time_ms = 1000, .pose = {.health = 24, .equip_flags = SST_MOTION_CAPABLE}};
        strcpy(ps[i].team, "coop");
    }
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 1100) == 1);
    ps[0].pose.equip_flags |= 0x80; // boss
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 1100) == 2);
    ps[1].pose.equip_flags |= 0x40; // cutscene
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 1100) == 3);
    ps[2].scene.room = 1;
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 1100) == 4);
    assert(!sst_motion_owner(4, false, &area, "coop", ps, 3, 1100));
    ps[0].pose.equip_flags = SST_MOTION_CAPABLE;
    assert(sst_motion_owner(4, true, &area, "other", ps, 3, 1100) == 4);
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 2501) == 4); // stale source
    ps[0].active = false;
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 1100) == 4); // disconnected
    ps[0].active = true; ps[0].pose.equip_flags = 0;
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 1100) == 4); // opted out
    ps[0].pose.equip_flags = SST_MOTION_CAPABLE; ps[0].pose.health = 0;
    assert(sst_motion_owner(4, true, &area, "coop", ps, 3, 1100) == 4); // dead player

    uint64_t key = sst_world_enemy_key(0x16d, 15, 123);
    SstEnemyMotion m = {.scene = area, .key = key, .profile = 0x16d, .from = 2,
        .sequence = 1, .received_ms = 1000, .pos = {100, 200, 300}, .velocity = {1, 2, 3}};
    assert(sst_motion_valid(&m));
    m.pos[0] = NAN; assert(!sst_motion_valid(&m)); m.pos[0] = 100;
    m.velocity[1] = INFINITY; assert(!sst_motion_valid(&m)); m.velocity[1] = 2;
    m.velocity[0] = 1001; assert(!sst_motion_valid(&m)); m.velocity[0] = 1;
    m.pos[1] = 1000001; assert(!sst_motion_valid(&m)); m.pos[1] = 200;
    m.profile = 0x193; assert(!sst_motion_valid(&m)); m.profile = 0x16d;
    m.key = sst_world_enemy_key(0x193, 15, 123); assert(!sst_motion_valid(&m)); m.key = key;
    strcpy(m.scene.stage, "B100"); assert(!sst_motion_valid(&m)); m.scene = area;
    m.key = sst_world_enemy_key(0x16d, 0xffff, 123); assert(!sst_motion_valid(&m)); m.key = key;

    SstMotionState s = {0}; sst_motion_begin_at(&s, &area, 100, 2, 1000);
    assert(!sst_motion_receive(&s, &m, 1000)); // no active matching instance
    sst_motion_observe(&s, key, 17, 0x16d);
    m.from = 3; assert(!sst_motion_receive(&s, &m, 1000)); m.from = 2;
    m.scene.layer = 1; assert(!sst_motion_receive(&s, &m, 1000)); m.scene.layer = 0;
    assert(!sst_motion_receive(&s, &m, 1501)); // queued during a load; too old
    assert(sst_motion_receive(&s, &m, 1000));
    assert(!sst_motion_receive(&s, &m, 1000));
    float p[3], v[3]; int16_t r[3];
    assert(sst_motion_sample(&s, key, 17, 1000, p, r, v));
    assert(p[0] == 100 && v[2] == 3);
    m.sequence = 2; m.received_ms = 1100; m.pos[0] = 200; m.rot[1] = 16384;
    assert(sst_motion_receive(&s, &m, 1100));
    assert(sst_motion_sample(&s, key, 17, 1150, p, r, v));
    assert(p[0] > 139 && p[0] < 141 && r[1] > 6500 && r[1] < 6600); // adaptive125ms interpolation
    assert(sst_motion_sample(&s, key, 17, 1225, p, r, v) && p[0] == 200);
    assert(!sst_motion_sample(&s, key, 17, 1601, p, r, v)); // no indefinite freeze
    assert(!sst_motion_sample(&s, key, 18, 1200, p, r, v));

    sst_motion_begin_at(&s, &area, 100, 3, 1200); // handoff retains a bounded old-owner track
    assert(sst_motion_sample(&s, key, 17, 1200, p, r, v));
    assert(!sst_motion_receive(&s, &m, 1200)); // duplicate old-owner sequence still rejected
    m.sequence = 3; m.received_ms = 1201; m.pos[0] = 220;
    assert(sst_motion_receive(&s, &m, 1201)); // old in-flight packet within400ms grace
    m.from = 3; m.sequence = 1; m.received_ms = 1250; m.pos[0] = 230;
    assert(sst_motion_receive(&s, &m, 1250));
    m.from = 2; m.sequence = 4; m.received_ms = 1260;
    assert(!sst_motion_receive(&s, &m, 1260)); // new owner has taken over this track

    memset(&s, 0, sizeof s); sst_motion_begin_at(&s, &area, 100, 3, 2000);
    sst_motion_observe(&s, key, 17, 0x16d);
    m.from = 3; m.sequence = 1; m.received_ms = 2000; m.rot[1] = 32760;
    assert(sst_motion_receive(&s, &m, 2000));
    m.sequence = 2; m.received_ms = 2133; m.rot[1] = -32760;
    assert(sst_motion_receive(&s, &m, 2133));
    assert(sst_motion_sample(&s, key, 17, 2199, p, r, v) && r[1] == -32768);
    sst_motion_observe(&s, key, 18, 0x16d); // respawn cannot reuse a dead actor's target
    assert(!sst_motion_sample(&s, key, 18, 2200, p, r, v));
    sst_motion_begin_at(&s, &area, 101, 3, 2200); // even reloading the same area starts afresh
    assert(!sst_motion_receive(&s, &m, 2200));
    sst_motion_observe(&s, key, 19, 0x16d);
    m.presence_generation=1; m.sequence=50; m.received_ms=2300; m.pos[0]=100;
    assert(sst_motion_receive(&s, &m, 2300));
    m.presence_generation=2; m.sequence=1; m.received_ms=2400; m.pos[0]=250;
    assert(sst_motion_receive(&s, &m, 2400)); // same owner ID, restarted movement stream
    assert(sst_motion_sample(&s, key, 19, 2400, p, r, v) && p[0]==250);
    m.presence_generation=1; m.sequence=999; m.received_ms=2500; m.pos[0]=900;
    assert(!sst_motion_receive(&s, &m, 2500));
    assert(sst_motion_sample(&s, key, 19, 2500, p, r, v) && p[0]==250);
    m.presence_generation=2; m.sequence=3; m.received_ms=2600+SST_MOTION_FUTURE_MS+1;
    assert(!sst_motion_receive(&s, &m, 2600)); // bounded future network stamps
    sst_motion_configure(false); assert(!sst_motion_enabled()); sst_motion_configure(true);
    puts("enemy motion: owner election, handoff, stale data, validation, interpolation, angles, respawns and bosses PASS");
}

#include "../src/sst_remote.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    SstScene scene = {.stage = "F001r", .room = 1, .layer = 1};
    SstPlayer p = {.active = true,
                   .id = 7,
                   .name = "Friend",
                   .scene = scene,
                   .has_pose = true,
                   .pose = {.tick = 1, .pos = {100, 0, 0}, .health = 24, .max_health = 24},
                   .pose_time_ms = 1000};
    SstRemote out[4];
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 1 && out[0].pose.pos[0] == 100);
    // Delayed old-room packets must not appear after either player changes areas.
    p.scene.room = 2;
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 0);
    p.scene = scene;
    p.scene.layer = 2;
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 0);
    p.scene = scene;
    assert(sst_remote_sample(&scene, &p, 1, 2001, out, 4) == 0);
    p.pose.pos[0] = NAN;
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 0);
    // Repeated departures and arrivals must recycle all interpolation slots.
    for (int i = 0; i < SST_MAX_PLAYERS * 4; i++) {
        p.id = 100 + i;
        p.pose.pos[0] = 100 + i;
        p.pose.tick = 1;
        assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 1);
        assert(out[0].pose.pos[0] == 100 + i && out[0].id == 100 + i);
    }
    scene.room = 2;
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 0);
    p.scene = scene;
    p.pose.pos[0] = 800;
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 1 && out[0].pose.pos[0] == 800);
    assert(sst_remote_sample(NULL, &p, 1, 1000, out, 4) == 0);
    // A same-ID reconnect can arrive completely between render frames, in the same area.
    p.pose.tick = 50; p.pose.pos[0] = 100; p.presence_generation = 1;
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 1 && out[0].pose.pos[0] == 100);
    p.pose.tick = 1; p.pose.pos[0] = 200; p.presence_generation = 2;
    assert(sst_remote_sample(&scene, &p, 1, 1000, out, 4) == 1 && out[0].pose.pos[0] == 200);
    puts("remote players: scene, layer, stale data, invalid poses, slot reuse and transition reset PASS");
}

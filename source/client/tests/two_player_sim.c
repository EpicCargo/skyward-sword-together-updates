// One simulated player walking a circle through a live relay; run two processes (run_sim.sh).
//   two_player_sim <port> <name> <peer>
// Checks that the peer's poses come out of the playout buffer smoothly. Exit 0 = success.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "../src/sst_net.h"
#include "../src/sst_pose_buffer.h"

#define SIM_FRAMES 300 // 10 seconds at 30 Hz
#define STAGE "F000"

typedef struct {
    const char *name;
    const char *peer;
    uint16_t port;
    int ok;
    int poses_received;
    float smoothness; // average position delta between consecutive interpolated frames
} SimResult;

static void walk_circle(int frame, float *pos) {
    float t = frame * 0.05f;
    pos[0] = cosf(t) * 500.0f;
    pos[1] = 0.0f;
    pos[2] = sinf(t) * 500.0f;
}

static void sim_player(SimResult *r) {

    SstConfig cfg = {.port = r->port, .auto_reconnect = true};
    snprintf(cfg.host, sizeof cfg.host, "127.0.0.1");
    snprintf(cfg.name, sizeof cfg.name, "%s", r->name);
    snprintf(cfg.team, sizeof cfg.team, "coop");

    sst_net_start(&cfg);

    SstScene scene = {.room = 1};
    snprintf(scene.stage, sizeof scene.stage, STAGE);
    sst_net_send_scene(&scene);

    SstPoseBuffer buf;
    sst_pose_buffer_reset(&buf);

    float last_x = 0, last_z = 0;
    double smooth_sum = 0;
    int smooth_count = 0;

    for (int frame = 0; frame < SIM_FRAMES; frame++) {
        float pos[3];
        walk_circle(frame, pos);
        SstPose pose = {
            .tick = (uint32_t)frame,
            .pos = {pos[0], pos[1], pos[2]},
            .rot = {0, (int16_t)(frame * 200), 0},
            .anim_id = 1,
            .anim_frame = frame * 0.5f,
            .health = 24,
            .max_health = 24,
            .equip_flags = 0x03,
        };
        sst_net_send_pose(&pose);

        // Poll for the peer's data
        SstPlayer ps[SST_MAX_PLAYERS];
        int n = sst_net_players(ps, SST_MAX_PLAYERS);
        for (int i = 0; i < n; i++) {
            if (strcmp(ps[i].name, r->peer) == 0 && ps[i].has_pose) {
                double now = sst_now_ms();
                sst_pose_buffer_push(&buf, &ps[i].pose, now);
            }
        }

        // Sample the interpolated pose
        SstPose interp;
        double now = sst_now_ms();
        if (sst_pose_buffer_sample(&buf, now, &interp)) {
            r->poses_received++;
            if (smooth_count > 0) {
                float dx = interp.pos[0] - last_x;
                float dz = interp.pos[2] - last_z;
                float dist = sqrtf(dx * dx + dz * dz);
                smooth_sum += dist;
            }
            last_x = interp.pos[0];
            last_z = interp.pos[2];
            smooth_count++;
        }

        usleep(33333); // ~30 Hz
    }

    // Let the peer finish seeing us too
    for (int k = 0; k < 30; k++) {
        float pos[3];
        walk_circle(SIM_FRAMES + k, pos);
        SstPose pose = {.tick = (uint32_t)(SIM_FRAMES + k), .pos = {pos[0], pos[1], pos[2]},
                        .health = 24, .max_health = 24};
        sst_net_send_pose(&pose);
        usleep(33333);
    }

    r->smoothness = smooth_count > 1 ? (float)(smooth_sum / (smooth_count - 1)) : 0;
    r->ok = r->poses_received > SIM_FRAMES / 3; // should receive a majority of frames
    sst_net_stop();
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <relay-port> <name> <peer>\n", argv[0]); return 2; }
    SimResult r = {.name = argv[2], .peer = argv[3], .port = (uint16_t)atoi(argv[1])};
    sim_player(&r);
    int smooth = r.smoothness < 50.0f;
    printf("%s: %d smoothed poses of %s, avg step %.2f units (limit 50): %s\n", r.name, r.poses_received, r.peer,
           r.smoothness, r.ok && smooth ? "OK" : "FAIL");
    return r.ok && smooth ? 0 : 1;
}

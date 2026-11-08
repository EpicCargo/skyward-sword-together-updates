// End-to-end check of sst_net against a real relay: run two of these processes (see run_e2e.sh).
//   net_e2e <port> <name> <expect-peer-name>
// Each sends its scene, poses and a flag, and exits 0 once it has seen the peer's pose and flag.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/sst_net.h"

static int got_flag;
static void on_flag(uint8_t kind, uint16_t index, uint8_t value, uint16_t from, void *user) {
    (void)from;
    const char *peer = user;
    // each side sets flag index 100 + first letter of its name
    if (kind == 1 && index == 100 + (uint8_t)peer[0] && value == 1) got_flag = 1;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s port name peer\n", argv[0]); return 2; }
    SstConfig cfg = {.port = (uint16_t)atoi(argv[1]), .auto_reconnect = true};
    snprintf(cfg.host, sizeof cfg.host, "127.0.0.1");
    snprintf(cfg.name, sizeof cfg.name, "%s", argv[2]);
    snprintf(cfg.team, sizeof cfg.team, "team");
    const char *peer = argv[3];
    sst_net_start(&cfg);

    // flag queued before the connection is up: must still arrive
    sst_net_send_flag(1, 100 + (uint8_t)argv[2][0], 1);
    SstScene scene = {.room = 2};
    snprintf(scene.stage, sizeof scene.stage, "F000");
    sst_net_send_scene(&scene);

    int seen_pose = 0;
    for (int frame = 0; frame < 30 * 10; frame++) {  // 10 s at 30 Hz
        SstPose pose = {.tick = (uint32_t)frame, .pos = {frame * 1.0f, 2.0f, 3.0f}, .rot = {0, (int16_t)(frame * 100), 0},
                        .anim_id = 7, .anim_frame = frame * 0.5f, .health = 24, .max_health = 24};
        sst_net_send_pose(&pose);
        sst_net_poll_flags(on_flag, (void *)peer);
        SstPlayer ps[SST_MAX_PLAYERS];
        int n = sst_net_players(ps, SST_MAX_PLAYERS);
        for (int i = 0; i < n; i++)
            if (!strcmp(ps[i].name, peer) && ps[i].has_pose && ps[i].pose.anim_id == 7 && ps[i].pose.pos[1] == 2.0f &&
                !strcmp(ps[i].scene.stage, "F000") && ps[i].scene.room == 2)
                seen_pose = 1;
        if (seen_pose && got_flag && sst_net_stats().has_rtt) {
            // keep sending a little longer so the peer also sees us
            for (int k = 0; k < 30; k++) { pose.tick++; sst_net_send_pose(&pose); usleep(33000); }
            printf("%s: ok (id %u, saw %s's pose and flag)\n", argv[2], sst_net_my_id(), peer);
            SstConnectionStats stats = sst_net_stats();
            if (!stats.online || stats.last_packet_age_ms > 2000 || stats.rtt_ms > 2000) return 1;
            sst_net_stop();
            stats = sst_net_stats();
            if (stats.online || stats.has_rtt || stats.queued_bytes) return 1;
            return 0;
        }
        usleep(33000);
    }
    char why[160];
    SstStatus st = sst_net_status(why, sizeof why);
    printf("%s: FAILED status=%d pose=%d flag=%d %s\n", argv[2], st, seen_pose, got_flag, why);
    sst_net_stop();
    return 1;
}

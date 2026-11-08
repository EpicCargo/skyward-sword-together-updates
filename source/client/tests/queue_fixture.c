// Public-client API fixture: the network thread runs while the consumer is deliberately held.
#include "../src/sst_net.h"
#include "../src/sst_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned callbacks, enemy_callbacks;
static unsigned char values[65536];
static SstEnemies enemies;
static uint64_t key;
static unsigned chest_callbacks;
static void chest(const SstChestReward *reward, void *user) { (void)reward; (void)user; chest_callbacks++; }
static void flag(uint8_t kind, uint16_t index, uint8_t value, uint16_t from, void *user) {
    (void)from; (void)user;
    if (kind == 1) { callbacks++; values[index] |= value; }
}
static void hit(const SstEnemyEvent *event, void *user) {
    (void)user; enemy_callbacks++; sst_enemies_receive(&enemies, event);
}
static void ignored(const SstEnemyEvent *event, void *user) { (void)event; (void)user; }
static void reset(void) {
    callbacks = enemy_callbacks = 0; memset(values, 0, sizeof values);
    memset(&enemies, 0, sizeof enemies);
    SstScene scene = {.stage = "F100"}; sst_enemies_begin(&enemies, &scene, 1);
    key = sst_world_enemy_key(0x16d, 12, 0x1234);
    sst_enemies_observe(&enemies, key, 1, 0x16d, 1000, ignored, NULL);
}
static void summary(void) {
    unsigned unique = 0, hp = 0;
    for (unsigned i = 0; i < 65536; i++) unique += values[i] != 0;
    for (unsigned i = 0; i < SST_ENEMIES; i++) if (enemies.actors[i].key == key) hp = enemies.actors[i].health;
    SstConnectionStats stats = sst_net_stats();
    printf("{\"callbacks\":%u,\"unique\":%u,\"value50\":%u,\"tail4050\":%u,"
           "\"overflow_tail\":%u,\"target\":%u,\"enemy_callbacks\":%u,\"health\":%u,\"recoveries\":%u}\n",
           callbacks, unique, values[50], values[4050], values[4099], values[65530],
           enemy_callbacks, hp, stats.queue_recoveries);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    setvbuf(stdout, NULL, _IONBF, 0); reset();
    SstConfig config = {.host="127.0.0.1", .port=(uint16_t)atoi(argv[1]), .name="Queue fixture"};
    sst_net_start(&config); puts("ready");
    char command[128], marker[SST_STR];
    while (fgets(command, sizeof command, stdin)) {
        if (!strncmp(command, "wait ", 5) && sscanf(command + 5, "%63s", marker) == 1) {
            bool found = false; uint64_t end = sst_now_ms() + 8000;
            do {
                SstPlayer players[SST_MAX_PLAYERS]; int n = sst_net_players(players, SST_MAX_PLAYERS);
                for (int i = 0; i < n; i++) found |= !strcmp(players[i].name, marker);
                if (!found) usleep(1000);
            } while (!found && sst_now_ms() < end);
            puts(found ? "marked" : "timeout");
        } else if (!strncmp(command, "offline-burst", 13)) {
            for (unsigned i=0;i<6000;i++) sst_net_send_flag(1, 7, 1);
            sst_net_send_flag(1, 7, 2); sst_net_send_flag(1, 8, 4); puts("queued");
        } else if (!strncmp(command, "flags", 5)) {
            sst_net_poll_flags(flag, NULL); summary();
        } else if (!strncmp(command, "request", 7)) {
            printf("{\"token\":%u}\n", sst_net_request_world_tracked());
        } else if (!strncmp(command, "refresh ", 8)) {
            unsigned token = 0; sscanf(command + 8, "%u", &token);
            printf("{\"state\":%u}\n", sst_net_world_refresh(token));
        } else if (!strncmp(command, "chests", 6)) {
            sst_net_poll_chests(chest, NULL); printf("{\"chests\":%u}\n", chest_callbacks);
        } else if (!strncmp(command, "enemies", 7)) {
            sst_net_poll_enemies(hit, NULL); summary();
        } else if (!strncmp(command, "stats", 5)) summary();
        else if (!strncmp(command, "reset", 5)) { reset(); puts("reset"); }
        else if (!strncmp(command, "quit", 4)) break;
        else { fprintf(stderr,"Unknown fixture command\n"); sst_net_stop(); return 2; }
    }
    sst_net_stop(); return 0;
}

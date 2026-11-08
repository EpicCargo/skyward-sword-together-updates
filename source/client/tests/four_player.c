// Four independent processes exercising the real C client against the embedded host.
#include "../src/sst_net.h"
#include "../src/sst_enemy_motion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static const unsigned quest_index[] = {4486,4547,4543,4735};
static unsigned trials_seen;
static const unsigned trial_indices[] = {4366,4367,4368,4369};
static const uint16_t trial_items[] = {68,138,20,180};
static unsigned quests_seen;
static unsigned claws_seen;
static unsigned gear_seen;
static unsigned upgrades_seen, gifts_seen;
static const uint16_t upgrade_items[] = {71,75,90,105};
static unsigned bags_seen;
static const uint16_t gear_items[] = {56,68,99,138};
static unsigned flags, pouch_seen, expansions_seen;
static const uint16_t pouch_items[] = {70,100,101,102,103,104,114,128,129,130,131,132,133,134,135,136};
static unsigned enemy_seen;
static unsigned chest_seen;
static unsigned keys_seen;
static unsigned hearts_seen;
static unsigned scene_chests_seen;
static unsigned maps_seen, boss_keys_seen, crystals_seen, ranged_seen;
static const unsigned crystal_index[] = {934,935,3620,3639};
static void chest_reward(const SstChestReward *r, void *user) {
    (void)user;
    if (r->index >= 764 && r->index < 768 && r->item == 113) expansions_seen |= 1u << (r->index - 764);
    if (r->index >= 780 && r->index < 796 && r->item == pouch_items[r->index - 780]) pouch_seen |= 1u << (r->index - 780);
    for (unsigned i=0;i<4;i++) if (r->index==trial_indices[i] && r->item==trial_items[i]) trials_seen |= 1u<<i;
    if (r->index >= 928 && r->index < 932 && r->item == upgrade_items[r->index - 928]) upgrades_seen |= 1u << (r->index - 928);
    if (r->index >= 924 && r->index < 928 && r->item == 92) bags_seen |= 1u << (r->index - 924);
    if (r->index >= 920 && r->index < 924 && r->item == gear_items[r->index - 920]) gear_seen |= 1u << (r->index - 920);
    if (r->index >= 916 && r->index < 920 && r->item == 20) claws_seen |= 1u << (r->index - 916);
    for (unsigned i = 0; i < 4; i++) if (r->index == quest_index[i] && r->item == 35) quests_seen |= 1u << i;
    for (unsigned i = 0; i < 4; i++)
        if (r->index == crystal_index[i] && r->item == 48) crystals_seen |= 1u << i;
    if (r->index >= 912 && r->index < 916 && r->item == (r->index % 2 ? 52 : 19)) ranged_seen |= 1u << (r->index - 912);
    if (r->index >= 100 && r->index < 104 && r->item == 169) chest_seen |= 1u << (r->index - 100);
    if (r->index >= 356 && r->index < 360 && r->item == 1) keys_seen |= 1u << (r->index - 356);
    if (r->index >= 600 && r->index < 604 && r->item == 94) hearts_seen |= 1u << (r->index - 600);
    if (r->index >= 896 && r->index < 900 && r->item == 94) scene_chests_seen |= 1u << (r->index - 896);
    if (r->index >= 362 && r->index < 366 && r->item == 50) maps_seen |= 1u << (r->index - 362);
    if (r->index >= 372 && r->index < 376 && r->item == 29) boss_keys_seen |= 1u << (r->index - 372);
}
static bool motion_seen;
static uint16_t expected_owner;
static void motion_event(const SstEnemyMotion *m, void *user) {
    (void)user;
    if (m->from == expected_owner && m->profile == 0x16d && m->pos[0] == 777 && m->velocity[0] == 2)
        motion_seen = true;
}
static void enemy(const SstEnemyEvent *e, void *user) {
    (void)user;
    if (e->from && e->snapshot && e->key >= 1 && e->key <= 4 && e->profile == 0x17e &&
        e->health == 21 - e->key && !strcmp(e->scene.stage, "F001r") && e->scene.room == 1 && e->scene.layer == 1)
        enemy_seen |= 1u << (e->key - 1);
}
static void flag(uint8_t kind, uint16_t bit, uint8_t value, uint16_t from, void *user) {
    (void)from;
    (void)user;
    if (kind == 1 && bit >= 9 && bit < 13 && value == 1)
        flags |= 1u << (bit - 9);
    for (unsigned i = 0; i < 4; i++)
        if (kind == 1 && bit == upgrade_items[i] && value == 1) gifts_seen |= 1u << i;
}
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    int index = atoi(argv[2]);
    SstConfig c = {.port = (uint16_t)atoi(argv[1]),
                   .host = "127.0.0.1",
                   .room = "coop",
                   .team = "team",
                   .auto_reconnect = true};
    snprintf(c.name, sizeof c.name, "Link %d", index + 1);
    sst_net_start(&c);
    SstScene scene = {.stage = "F001r", .room = 1, .layer = 1, .entrance = 5};
    sst_net_send_scene(&scene);
    sst_net_send_flag(1, 9 + index, 1);
    flags = 1u << index;
    expansions_seen = 1u << index;
    pouch_seen = 15u << (4 * index);
    trials_seen = 1u << index;
    upgrades_seen = gifts_seen = 1u << index;
    sst_net_send_flag(1, upgrade_items[index], 1);
    enemy_seen = 1u << index;
    chest_seen = 1u << index;
    keys_seen = 1u << index;
    hearts_seen = 1u << index;
    scene_chests_seen = 1u << index;
    maps_seen = boss_keys_seen = crystals_seen = ranged_seen = quests_seen = claws_seen = gear_seen = bags_seen = 1u << index;
    bool requested = false;
    unsigned seen = 1u << index;
    int success_at = -1;
    for (int frame = 0; frame < 450; frame++) {
        SstPose pose = {.tick = frame + 1, .pos = {index * 100, 1.5f, -2700}, .health = 24, .max_health = 24,
            .equip_flags = SST_MOTION_CAPABLE};
        sst_net_send_pose(&pose);
        expected_owner = sst_net_motion_owner(&scene, true);
        if (expected_owner && expected_owner == sst_net_my_id()) {
            motion_seen = true;
            if (frame % 3 == 0) {
                SstEnemyMotion m = {.scene = scene, .key = sst_world_enemy_key(0x16d, 3, 123),
                    .profile = 0x16d, .sequence = frame + 1, .pos = {777, 250, -1300}, .velocity = {2, 0, 0}};
                sst_net_send_motion(&m);
            }
        }
        sst_net_poll_motion(motion_event, NULL);
        sst_net_poll_flags(flag, NULL);
        sst_net_poll_enemies(enemy, NULL);
        sst_net_poll_chests(chest_reward, NULL);
        requested |= sst_net_take_world_request();
        if (frame % 30 == 0) {
            sst_net_request_world();
            { SstChestReward expansion = {.index=764+index,.item=113}; sst_net_send_chest(&expansion); }
            for (unsigned j = 4 * index; j < 4 * index + 4; j++) {
                SstChestReward pouch = {.index = 780 + j, .item = pouch_items[j]}; sst_net_send_chest(&pouch);
            }
            SstChestReward r = {.index = 100 + index, .item = 169};
            sst_net_send_chest(&r);
            r.index = 356 + index; r.item = 1; sst_net_send_chest(&r);
            r.index = 600 + index; r.item = 94; sst_net_send_chest(&r);
            r.index = 896 + index; sst_net_send_chest(&r);
            r.index = 362 + index; r.item = 50; sst_net_send_chest(&r);
            r.index = 372 + index; r.item = 29; sst_net_send_chest(&r);
            r.index = 916 + index; r.item = 20; sst_net_send_chest(&r);
            r.index = 920 + index; r.item = gear_items[index]; sst_net_send_chest(&r);
            r.index = 924 + index; r.item = 92; sst_net_send_chest(&r);
            r.index = 928 + index; r.item = upgrade_items[index]; sst_net_send_chest(&r);
            { SstChestReward trial = {.index=trial_indices[index], .item=trial_items[index]}; sst_net_send_chest(&trial); }
            r.index = quest_index[index]; r.item = 35; sst_net_send_chest(&r);
            r.index = crystal_index[index]; r.item = 48; sst_net_send_chest(&r);
            r.index = 912 + index; r.item = index % 2 ? 52 : 19; sst_net_send_chest(&r);
            SstEnemyEvent e = {.scene = scene, .key = index + 1, .sequence = frame + 1,
                .profile = 0x17e, .health = 20 - index, .snapshot = true};
            sst_net_send_enemy(&e);
        }
        SstPlayer players[SST_MAX_PLAYERS];
        int n = sst_net_players(players, SST_MAX_PLAYERS);
        for (int i = 0; i < n; i++)
            if (players[i].has_pose && players[i].scene.layer == 1 && players[i].scene.entrance == 5) {
                int k;
                if (sscanf(players[i].name, "Link %d", &k) == 1 && k >= 1 && k <= 4 &&
                    players[i].pose.pos[0] == (k - 1) * 100 && players[i].pose.health == 24)
                    seen |= 1u << (k - 1);
            }
        if (expansions_seen == 15 && pouch_seen == 65535 && trials_seen == 15 && seen == 15 && flags == 15 && enemy_seen == 15 && chest_seen == 15 && keys_seen == 15 && hearts_seen == 15 && scene_chests_seen == 15 && maps_seen == 15 && boss_keys_seen == 15 && crystals_seen == 15 && ranged_seen == 15 && claws_seen == 15 && gear_seen == 15 && bags_seen == 15 && upgrades_seen == 15 && gifts_seen == 15 && quests_seen == 15 && requested && motion_seen && sst_net_stats().has_rtt && success_at < 0)
            success_at = frame;
        if (success_at >= 0 && frame - success_at > 60) {
            printf("%s: saw all peers, progress, chests, enemy damage/movement and recovery requests (id %u)\n", c.name, sst_net_my_id());
            sst_net_stop();
            return 0;
        }
        usleep(33000);
    }
    fprintf(stderr, "%s: peers %#x flags %#x enemies %#x chests %#x keys %#x hearts %#x scenes %#x maps %#x bosskeys %#x crystals %#x ranged %#x request %d motion %d\n",
        c.name, seen, flags, enemy_seen, chest_seen, keys_seen, hearts_seen, scene_chests_seen, maps_seen, boss_keys_seen, crystals_seen, ranged_seen, requested, motion_seen);
    sst_net_stop();
    return 1;
}

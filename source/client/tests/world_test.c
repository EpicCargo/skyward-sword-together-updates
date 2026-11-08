#include "../src/sst_world.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static SstEnemyEvent sent[64];
static int count;
static void send_event(const SstEnemyEvent *e, void *u) { (void)u; assert(count < 64); sent[count++] = *e; }
static SstEnemies held_actors;
static unsigned held_calls, held_mutations;
static void held_event(const SstEnemyEvent *e, uint32_t instance, void *u) {
    (void)u; held_calls++;
    // The bridge must also bind delayed events to the actor that existed on arrival.
    for (unsigned i = 0; i < SST_ENEMIES; i++)
        if (held_actors.actors[i].used && held_actors.actors[i].key == e->key &&
            held_actors.actors[i].instance == instance) {
            if (sst_enemies_receive(&held_actors, e)) held_mutations++;
            break;
        }
}
static uint16_t held_health(uint64_t key) {
    for (unsigned i=0;i<SST_ENEMIES;i++)
        if (held_actors.actors[i].used && held_actors.actors[i].key==key) return held_actors.actors[i].health;
    assert(!"missing held actor"); return 0;
}
static void check_inbox(void) {
    SstScene scene = {.stage="F100", .room=0, .layer=0};
    SstEnemyInbox q = {0};
    uint64_t key = sst_world_enemy_key(0x16d, 12, 0x1234);
    SstEnemyEvent hit = {.scene=scene, .key=key, .from=2, .sequence=1,
        .profile=0x16d, .damage=3, .health=17};
    sst_enemy_inbox_begin(&q, &scene, 10);
    sst_enemies_begin(&held_actors, &scene, 10);
    sst_enemies_observe(&held_actors, key, 100, 0x16d, 20, send_event, NULL);
    assert(sst_enemy_inbox_push(&q, &hit, 100));
    assert(!sst_enemy_inbox_push(&q, &hit, 100)); // duplicate during a long menu
    hit.from=3; hit.sequence=1; hit.damage=4; hit.health=16;
    assert(sst_enemy_inbox_push(&q, &hit, 100));
    sst_enemy_inbox_begin(&q, &scene, 10); assert(q.count==2); // menu/event tick
    assert(held_health(key)==20); // untouched until resume
    assert(!sst_enemy_inbox_flush(&q, held_event, NULL));
    assert(q.count==0 && held_calls==2 && held_mutations==2);
    assert(held_health(key)==13); // both simultaneous hits count
    hit.from=2;hit.sequence=2;hit.damage=0;hit.health=0;hit.snapshot=true;
    assert(sst_enemy_inbox_push(&q,&hit,100));
    assert(!sst_enemy_inbox_flush(&q,held_event,NULL));
    assert(held_health(key)==0 && held_mutations==3);
    // A native respawn during an event cannot inherit delayed damage for its predecessor.
    hit.sequence=3;hit.snapshot=false;hit.damage=2;hit.health=18;
    assert(sst_enemy_inbox_push(&q,&hit,100));
    sst_enemies_observe(&held_actors,key,101,0x16d,20,send_event,NULL);
    assert(!sst_enemy_inbox_flush(&q,held_event,NULL));
    assert(held_health(key)==20 && held_mutations==3);
    assert(sst_enemy_inbox_push(&q,&hit,101));
    sst_enemy_inbox_begin(&q,&scene,11); assert(!q.count); // same-room reload
    assert(sst_enemy_inbox_push(&q,&hit,101));
    scene.room=1;sst_enemy_inbox_begin(&q,&scene,11);assert(!q.count);
    assert(!sst_enemy_inbox_push(&q,&hit,101));
    scene.room=0;scene.layer=1;sst_enemy_inbox_begin(&q,&scene,11);
    assert(!sst_enemy_inbox_push(&q,&hit,101));
    scene.layer=0;sst_enemy_inbox_begin(&q,&scene,11);
    hit.profile=0x193;assert(!sst_enemy_inbox_push(&q,&hit,101));hit.profile=0x16d;
    hit.key++;assert(sst_enemy_inbox_push(&q,&hit,101)); // another placed enemy
    q.count=0;hit.key=key ^ (1ull<<16);assert(!sst_enemy_inbox_push(&q,&hit,101));
    hit.key=sst_world_enemy_key(0x16d,0xffff,0x1234);assert(!sst_enemy_inbox_push(&q,&hit,101));hit.key=key;
    hit.health=1001;assert(!sst_enemy_inbox_push(&q,&hit,101));hit.health=18;
    hit.damage=1001;assert(!sst_enemy_inbox_push(&q,&hit,101));hit.damage=2;
    hit.snapshot=true;assert(!sst_enemy_inbox_push(&q,&hit,101));hit.snapshot=false;
    hit.from=0;assert(!sst_enemy_inbox_push(&q,&hit,101));hit.from=2;
    assert(!sst_enemy_inbox_push(&q,&hit,0));
    for (unsigned i=0;i<SST_ENEMY_PENDING;i++) {hit.sequence=10+i;assert(sst_enemy_inbox_push(&q,&hit,101));}
    hit.sequence++;assert(!sst_enemy_inbox_push(&q,&hit,101));assert(q.overflow);
    assert(sst_enemy_inbox_flush(&q,held_event,NULL));assert(!q.count && !q.overflow);
    assert(!sst_enemy_inbox_flush(&q,held_event,NULL));
    strcpy(scene.stage,"S000");hit.scene=scene;sst_enemy_inbox_begin(&q,&scene,12);
    assert(!sst_enemy_inbox_push(&q,&hit,101));
    strcpy(scene.stage,"B100");hit.scene=scene;sst_enemy_inbox_begin(&q,&scene,12);
    assert(!sst_enemy_inbox_push(&q,&hit,101));
}
static void check_rejoined_sender(void) {
    SstScene scene = {.stage="F100"};
    uint64_t key = sst_world_enemy_key(0x16d, 12, 0x1234);
    SstEnemyInbox inbox = {0}; memset(&held_actors, 0, sizeof held_actors);
    sst_enemies_begin(&held_actors, &scene, 100);
    sst_enemies_observe(&held_actors, key, 100, 0x16d, 20, send_event, NULL);
    sst_enemy_inbox_begin(&inbox, &scene, 100);
    SstEnemyEvent hit = {.scene=scene, .key=key, .from=2, .sequence=1,
        .profile=0x16d, .damage=2, .health=18, .presence_generation=1};
    assert(sst_enemy_inbox_push(&inbox, &hit, 100));
    hit.presence_generation=2; // reconnect while our native map is still open
    assert(sst_enemy_inbox_push(&inbox, &hit, 100)); // same sequence is a distinct hit
    assert(!sst_enemy_inbox_push(&inbox, &hit, 100)); // duplicate in the new stream
    assert(!sst_enemy_inbox_flush(&inbox, held_event, NULL));
    assert(held_health(key)==16); // both pre- and post-rejoin damage survived
    hit.presence_generation=1; hit.sequence=999;
    assert(!sst_enemies_receive(&held_actors, &hit)); // delayed old stream cannot replace the new one
    assert(held_health(key)==16);
}
int main(void) {
    check_inbox();
    check_rejoined_sender();
    // Song ownership is set-only progress; it must not widen unreviewed chest rewards.
    SstWorld songs={0};
    for(unsigned item=186;item<=193;item++) {
        assert(sst_world_song(item) && sst_world_flag_allowed(SST_WORLD_ITEM,item));
        assert(!sst_world_receive(&songs,SST_WORLD_ITEM,item,0));
        assert(sst_world_receive(&songs,SST_WORLD_ITEM,item,1));
        assert(sst_world_pending(&songs,SST_WORLD_ITEM,item));
        assert(!sst_world_learn(&songs,SST_WORLD_ITEM,item));
        sst_world_applied(&songs,SST_WORLD_ITEM,item);
        assert(!sst_world_pending(&songs,SST_WORLD_ITEM,item));
        assert(sst_world_receive(&songs,SST_WORLD_ITEM,item,1)); // replay repairs a rollback
        SstChestReward unsupported={.index=2,.item=item};assert(!sst_chest_valid(&unsupported));
    }
    assert(!sst_world_song(185) && !sst_world_song(194) && !sst_world_song(65535));
    SstWorld w = {0};
    assert(sst_world_receive(&w,SST_WORLD_STORY,22,1)); // bridge holds until personal Hylia reward is ready
    assert(sst_world_receive(&w, SST_WORLD_STORY, 1195, 1));
    assert(sst_world_pending(&w, SST_WORLD_STORY, 1195));
    assert(!sst_world_learn(&w, SST_WORLD_STORY, 1195)); // remote state cannot echo
    sst_world_applied(&w, SST_WORLD_STORY, 1195);
    assert(!sst_world_pending(&w, SST_WORLD_STORY, 1195));
    assert(sst_world_receive(&w, SST_WORLD_STORY, 1195, 1)); // replay can repair a rollback
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 1195, 0));
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 1195, 255));
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 5, 1)); // Skyview boss completion
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 7, 1));
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 11, 1));
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 13, 1));
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 15, 1));
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 136, 1)); // Imprisoned event trigger
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 288, 1)); // Bilocyte event trigger
    assert(!sst_world_receive(&w, SST_WORLD_STORY, 775, 1)); // boss refight dialogue
    assert(!sst_world_receive(&w, 255, 65535, 1));
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 180)); // Stone needs its trial receipt
    assert(!sst_world_receive(&w,SST_WORLD_ITEM,180,1));
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 2)); // rupees
    for (unsigned item=108;item<=111;item++) assert(!sst_world_flag_allowed(SST_WORLD_ITEM,item));
    for (unsigned item = 0; item < 512; item++) if (sst_chest_pouch_quantity(item))
        assert(!sst_world_flag_allowed(SST_WORLD_ITEM, item));
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 113)); // additive capacity needs receipts
    assert(!sst_world_receive(&w,SST_WORLD_ITEM,113,1));
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 126)); // potion bottles are native pouch objects
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 153)); // bottles are pouch objects
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 93)); // boss heart containers remain personal
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 94)); // heart pieces require complete transactions
    for (unsigned i = 0; i < 3; i++) {
        unsigned tool = (unsigned[]){49,53,137}[i];
        assert(!sst_world_flag_allowed(SST_WORLD_ITEM, tool));
        assert(!sst_world_receive(&w, SST_WORLD_ITEM, tool, 1)); // no ownership without a receipt
    }
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 19)); // Bow requires ownership/ammo/receipt
    assert(!sst_world_flag_allowed(SST_WORLD_ITEM, 92)); // Bomb Bag requires ownership/wheel/receipt
    assert(sst_world_flag_allowed(SST_WORLD_ITEM, 52)); // slingshot
    for (unsigned i = 0; i < 8; i++) {
        unsigned id = (unsigned[]){71,75,76,77,90,91,105,140}[i];
        assert(sst_world_flag_allowed(SST_WORLD_ITEM, id));
        assert(sst_world_receive(&w, SST_WORLD_ITEM, id, 1));
    }
    assert(!sst_world_receive(&w, SST_WORLD_ITEM, 65536 + 20, 1));
    assert(sst_world_receive(&w, SST_WORLD_SCENE, 17, 1));
    assert(sst_world_pending(&w, SST_WORLD_SCENE, 17));
    sst_world_protect_scene(&w, 17); // newly discovered chest receipt removes queued switch writes
    assert(!sst_world_pending(&w, SST_WORLD_SCENE, 17));
    assert(!sst_world_receive(&w, SST_WORLD_SCENE, 17, 1));
    assert(!sst_world_learn(&w, SST_WORLD_SCENE, 17));
    assert(!(w.known[SST_WORLD_SCENE][17 / 8] & (1u << (17 % 8))));
    assert(sst_world_receive(&w, SST_WORLD_SCENE, 18, 1)); // unrelated switches still work
    for (unsigned i = 0; i < SST_WORLD_BITS; i++) {
        bool piece = i == 520 || i == 521 || i == 522 || i == 524 || i == 576 ||
            i == 161 || i == 196 || i == 614 || i == 744 || i == 1511 ||
            (i >= 70 && i <= 75) || i == 102 || i == 103 || i == 104 || i == 106 || (i >= 108 && i <= 115) ||
            i == 2788 || i == 2804 || i == 2807 || i == 32 || i == 718 || i == 1238 || i == 2325;
        assert(sst_world_flag_allowed(SST_WORLD_SCENE, i) == !piece);
        if (piece) assert(!sst_world_receive(&w, SST_WORLD_SCENE, i, 1));
    }
    for (unsigned i = 0; i < 41; i++) {
        unsigned flag = (unsigned[]){35,326,360,361,373,382,383,384,386,387,394,395,398,472,473,477,478,481,482,547,548,549,550,551,552,553,554,555,556,557,558,559,560,561,575,635,636,639,663,664,771}[i];
        assert(!sst_world_flag_allowed(SST_WORLD_STORY, flag));
        assert(!sst_world_receive(&w, SST_WORLD_STORY, flag, 1));
    }
    for (unsigned id = 206; id <= 210; id++) {
        assert(sst_chest_trial_reserved(id));
        assert(!sst_world_receive(&w, SST_WORLD_STORY, id, 1));
    }
    assert(!sst_chest_trial_reserved(205) && !sst_chest_trial_reserved(211));
    assert(sst_world_trial_stage("S000") && sst_world_trial_stage("S100") &&
           sst_world_trial_stage("S200") && sst_world_trial_stage("S300"));
    assert(!sst_world_trial_stage("F300") && !sst_world_trial_stage("S301"));
    assert(!sst_world_enemy_profile(0x159)); // Guardians retain their native trial simulation.
    assert(sst_world_receive(&w, SST_WORLD_ITEM, 52, 1)); // other kinds are independent
    sst_world_protect_scene(&w, SST_WORLD_BITS); // bounds check
    assert(sst_world_boolean_definition(127, 0xf0, 128));
    assert(!sst_world_boolean_definition(128, 0, 128));
    assert(!sst_world_boolean_definition(0, 3, 128)); // four-bit counter
    for (unsigned t = 0; t < 26; t++) {
        for (unsigned b = 0; b < 128; b++) assert(!sst_world_flag_allowed(SST_WORLD_DUNGEON, t * 128 + b));
    }
    SstPlayer peer = {.active=true, .id=7, .team="team", .has_pose=true, .pose_time_ms=1000,
        .scene={.stage="F001r"}, .pose={.health=24, .max_health=24, .action=2}};
    assert(sst_world_catchup_allowed(&peer, "team", 1000));
    assert(!sst_world_catchup_allowed(&peer, "other", 1000));
    assert(!sst_world_catchup_allowed(&peer, "team", 999));
    assert(!sst_world_catchup_allowed(&peer, "team", 3001));
    assert(sst_world_catchup_status(&peer, "team", 1000) == SST_CATCHUP_READY);
    assert(sst_world_catchup_status(&peer, "other", 1000) == SST_CATCHUP_OTHER_TEAM);
    assert(sst_world_catchup_status(NULL, "team", 1000) == SST_CATCHUP_UNAVAILABLE);
    assert(sst_world_catchup_status(&peer, "team", 3000) == SST_CATCHUP_READY);
    assert(sst_world_catchup_status(&peer, "team", 3001) == SST_CATCHUP_STALE);
    assert(sst_world_catchup_status(&peer, "team", 999) == SST_CATCHUP_STALE);
    peer.pose.health=0; peer.pose.equip_flags=0xc0;
    assert(sst_world_catchup_status(&peer,"team",1000) == SST_CATCHUP_DEFEATED);
    assert(!strcmp(sst_world_catchup_text(SST_CATCHUP_DEFEATED), "Defeated"));
    assert(sst_world_catchup_status(&peer,"team",3001) == SST_CATCHUP_STALE);
    peer.pose.health=24;
    assert(sst_world_catchup_status(&peer,"team",1000) == SST_CATCHUP_BOSS);
    peer.pose.equip_flags=0x40;
    assert(sst_world_catchup_status(&peer,"team",1000) == SST_CATCHUP_MENU);
    peer.pose.equip_flags=0; peer.pose.max_health=20;
    assert(sst_world_catchup_status(&peer,"team",1000) == SST_CATCHUP_INVALID_HEALTH);
    peer.pose.max_health=24; peer.pose.action=120;
    assert(sst_world_catchup_status(&peer,"team",1000) == SST_CATCHUP_BUSY);
    peer.pose.action=2;
    SstPlayer malformed = peer;
    memset(malformed.scene.stage, 'F', sizeof malformed.scene.stage);
    assert(sst_world_catchup_status(&malformed,"team",1000) == SST_CATCHUP_NO_AREA);
    malformed=peer; malformed.active=false;
    assert(sst_world_catchup_status(&malformed,"team",1000) == SST_CATCHUP_UNAVAILABLE);
    strcpy(malformed.scene.stage,"B100"); malformed.active=true;
    assert(sst_world_catchup_status(&malformed,"team",1000) == SST_CATCHUP_BOSS);
    malformed=peer; strcpy(malformed.scene.stage,"S300");
    assert(sst_world_catchup_status(&malformed,"team",1000) == SST_CATCHUP_TRIAL);
    assert(!sst_world_catchup_allowed(&malformed,"team",1000));
    assert(!sst_world_catchup_request_allowed(&malformed,"team"));
    assert(sst_world_catchup_request_allowed(&peer, "team")); // button can refresh an old area pose
    assert(!sst_world_catchup_request_allowed(&peer, "other"));
    peer.has_pose=false; assert(sst_world_catchup_request_allowed(&peer,"team"));
    assert(!sst_world_catchup_allowed(&peer,"team",1000)); peer.has_pose=true;
    peer.pose.health=0; assert(!sst_world_catchup_allowed(&peer,"team",1000)); peer.pose.health=24;
    peer.pose.equip_flags=0x40; assert(!sst_world_catchup_allowed(&peer,"team",1000));
    peer.pose.equip_flags=0x80; assert(!sst_world_catchup_allowed(&peer,"team",1000)); peer.pose.equip_flags=0;
    peer.pose.action=120; assert(!sst_world_catchup_allowed(&peer,"team",1000)); peer.pose.action=2;
    strcpy(peer.scene.stage,"B100"); assert(!sst_world_catchup_allowed(&peer,"team",1000));
    strcpy(peer.scene.stage,"F403"); assert(!sst_world_catchup_allowed(&peer,"team",1000));
    strcpy(peer.scene.stage,"../bad"); assert(!sst_world_catchup_allowed(&peer,"team",1000));
    strcpy(peer.scene.stage,"D100"); assert(sst_world_catchup_allowed(&peer,"team",1000));
    assert(sst_world_boss_stage("B100")); assert(sst_world_boss_stage("F406"));
    assert(!sst_world_boss_stage("F402"));
    assert(!sst_world_boss_stage("D000"));
    for (unsigned p = 0; p < 0x300; p++) assert(!(sst_world_boss_profile(p) && sst_world_enemy_profile(p)));
    assert(sst_world_boss_profile(0x193)); assert(sst_world_boss_profile(0x14d));
    assert(sst_world_boss_actor(0x168, 7, 7)); // miniboss in the current room
    assert(!sst_world_boss_actor(0x168, 7, 0)); // instantiated in another room
    assert(sst_world_boss_actor(0x168, 0xff, 0)); // global boss actor
    assert(!sst_world_boss_actor(0x16d, 0xff, 0)); // global regular enemy
    assert(!sst_world_enemy_profile(0xc8)); assert(sst_world_enemy_profile(0x16d));

    SstScene scene = {.stage = "D000", .room = 0, .layer = 0};
    uint64_t key = sst_world_enemy_key(0x17e, 42, 0x12345678);
    assert(key != sst_world_enemy_key(0x17e, 43, 0x12345678));
    static SstEnemies a = {0}, b = {0};
    sst_enemies_begin(&a, &scene, 1); sst_enemies_begin(&b, &scene, 2);
    sst_enemies_observe(&a, key, 101, 0x17e, 20, send_event, NULL);
    sst_enemies_observe(&b, key, 202, 0x17e, 20, send_event, NULL);
    assert(count == 0);
    // Two simultaneous local hits must both count.
    sst_enemies_observe(&a, key, 101, 0x17e, 17, send_event, NULL);
    sst_enemies_observe(&b, key, 202, 0x17e, 16, send_event, NULL);
    assert(count == 2 && sent[0].damage == 3 && sent[1].damage == 4);
    sent[0].from = 1; sent[1].from = 2;
    SstEnemyTrack *ta = sst_enemies_receive(&a, &sent[1]);
    SstEnemyTrack *tb = sst_enemies_receive(&b, &sent[0]);
    assert(ta && tb && ta->health == 13 && tb->health == 13);
    assert(!sst_enemies_receive(&b, &sent[0]));
    sst_enemies_observe(&a, key, 101, 0x17e, 13, send_event, NULL);
    assert(count == 2); // applying damage must not send another hit
    SstEnemyEvent e = sent[0]; e.sequence = 20; e.health = 0; e.damage = 0; e.snapshot = true;
    e.scene.room = 1; assert(!sst_enemies_receive(&b, &e));
    e.scene.room = 0; e.scene.layer = 1; assert(!sst_enemies_receive(&b, &e));
    e.scene.layer = 0; e.profile = 0x193; assert(!sst_enemies_receive(&b, &e));
    e.profile = 0x17e; e.key++; assert(!sst_enemies_receive(&b, &e));
    e.key = key; e.sequence++;
    assert(sst_enemies_receive(&b, &e)->health == 0);
    sst_enemies_observe(&b, key, 202, 0x17e, 0, send_event, NULL);
    assert(count == 2);
    sst_enemies_announce(&b, send_event, NULL);
    assert(count == 3 && sent[2].snapshot && sent[2].health == 0);
    sst_enemies_begin(&b, &scene, 2);
    sst_enemies_announce(&b, send_event, NULL); // dead actors still help a late-arriving teammate
    assert(count == 4 && sent[3].health == 0);
    sst_enemies_begin(&b, &scene, 3); // loading the room again permits its ordinary respawns
    sst_enemies_observe(&b, key, 303, 0x17e, 20, send_event, NULL);
    assert(count == 4);
    // A respawn in the same room is also a fresh actor instance.
    sst_enemies_observe(&b, key, 304, 0x17e, 20, send_event, NULL);
    sst_enemies_observe(&b, key, 304, 0x17e, 19, send_event, NULL);
    assert(count == 5 && sent[4].damage == 1);
    sst_world_request(SST_RECOVER_PEER, 12); uint16_t id;
    assert(sst_world_take_request(&id) == SST_RECOVER_PEER && id == 12);
    assert(sst_world_take_request(&id) == SST_RECOVER_NONE);
    // A refresh cannot discard a requested transition, including between guest ticks.
    sst_world_request(SST_RECOVER_RELOAD, 0); sst_world_request(SST_RECOVER_SYNC, 0);
    assert(sst_world_take_request(&id) == SST_RECOVER_RELOAD && id == 0);
    SstRecovery pending = SST_RECOVER_PEER; id = 12;
    assert(!sst_world_merge_request(&pending, &id, SST_RECOVER_SYNC, 0));
    assert(pending == SST_RECOVER_PEER && id == 12);
    assert(!sst_world_merge_request(&pending, &id, SST_RECOVER_PEER, 12));
    assert(sst_world_merge_request(&pending, &id, SST_RECOVER_PEER, 13) && id == 13);
    assert(sst_world_merge_request(&pending, &id, SST_RECOVER_RELOAD, 99) && id == 0);
    assert(!sst_world_merge_request(&pending, &id, SST_RECOVER_PEER, 0));
    assert(!sst_world_merge_request(&pending, &id, SST_RECOVER_NONE, 0));
    sst_world_configure(false, true); bool p, en;
    sst_world_options(&p, &en); assert(!p && en);
    sst_world_status_set(true, 2, 3, "Boss encounter"); assert(sst_world_in_boss());
    sst_world_reset(); assert(!sst_world_in_boss());
    puts("world sync: policy, counters, boss exclusions, concurrent damage, no echo, dedupe, rooms, respawns and recovery PASS");
}

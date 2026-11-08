// Portable, game-free peer: real production transport, stdin control, JSON diagnostics.
#include "../src/sst_net.h"
#include "../src/sst_world.h"
#include "../src/sst_enemy_motion.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static atomic_bool alive=true, paused;
static atomic_int wanted_room;
static atomic_int wanted_actors=1;
static unsigned motions, flag_value, rewards, damage_events;
static uint32_t motion_sequence;
static uint8_t motion_keys[SST_ENEMIES];
static void motion(const SstEnemyMotion *m, void *unused) {
    (void)unused; motions++; motion_sequence=m->sequence;
    unsigned id=(uint16_t)m->key;
    if (id>=15 && id<15+SST_ENEMIES) motion_keys[id-15]=1;
}
static void flag(uint8_t kind, uint16_t index, uint8_t value, uint16_t from, void *unused) {
    (void)from; (void)unused; if (kind==1 && index==321) flag_value|=value;
}
static void reward(const SstChestReward *r, void *unused) { (void)unused; if (r->index==831) rewards++; }
static void enemy(const SstEnemyEvent *e, void *unused) { (void)e; (void)unused; damage_events++; }
static void *stream(void *unused) {
    (void)unused; int room=-1; uint32_t tick=0;
    SstScene scene={.stage="D000"};
    while (alive) {
        int next=wanted_room;
        if (next!=room) { room=next; scene.room=(uint8_t)room; sst_net_send_scene(&scene); }
        if (!paused) {
            SstPose p={.tick=++tick,.pos={100,200,300},.health=24,.max_health=24,.equip_flags=SST_MOTION_CAPABLE};
            sst_net_send_pose(&p);
            if (sst_net_my_id() && sst_net_motion_owner(&scene,true)==sst_net_my_id())
                for (int i=0;i<wanted_actors;i++) {
                    SstEnemyMotion m={.scene=scene,.key=sst_world_enemy_key(0x16d,15+i,123),.sequence=tick,
                        .profile=0x16d,.pos={100,200,300},.velocity={1,2,3}};
                    sst_net_send_motion(&m);
                }
        }
        usleep(33333);
    }
    return NULL;
}
int main(int argc, char **argv) {
    if (argc!=4) return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    SstConfig config={.host="127.0.0.1",.port=(uint16_t)atoi(argv[1]),.room="coop",.team="team",
        .use_udp=atoi(argv[2])!=0,.auto_reconnect=true};
    snprintf(config.name,sizeof config.name,"%s",argv[3]);
    sst_net_start(&config); pthread_t worker; if (pthread_create(&worker,NULL,stream,NULL)) return 2;
    puts("ready"); char command[128];
    while (fgets(command,sizeof command,stdin)) {
        if (!strncmp(command,"stats",5)) {
            sst_net_poll_motion(motion,NULL); sst_net_poll_flags(flag,NULL); sst_net_poll_chests(reward,NULL);
            sst_net_poll_enemies(enemy,NULL);
            SstConnectionStats s=sst_net_stats(); SstPlayer peers[SST_MAX_PLAYERS];
            int count=sst_net_players(peers,SST_MAX_PLAYERS); uint32_t tick=0; unsigned poses=0;
            unsigned unique=0; for (unsigned i=0;i<SST_ENEMIES;i++) unique+=motion_keys[i];
            for (int i=0;i<count;i++) if (peers[i].has_pose) { poses++; if (peers[i].pose.tick>tick) tick=peers[i].pose.tick; }
            printf("{\"online\":%d,\"id\":%u,\"udp\":%d,\"available\":%d,\"sent\":%u,\"received\":%u,"
                "\"fallbacks\":%u,\"players\":%d,\"poses\":%u,\"tick\":%u,\"motions\":%u,"
                "\"motion_sequence\":%u,\"flag\":%u,\"rewards\":%u,\"damage_events\":%u,\"motion_unique\":%u,\"queued\":%llu,\"generation\":%u}\n",s.online,sst_net_my_id(),s.udp_active,
                s.udp_available,s.udp_sent,s.udp_received,s.udp_fallbacks,count,poses,tick,motions,motion_sequence,flag_value,rewards,damage_events,unique,(unsigned long long)s.queued_bytes,sst_net_generation());
        } else if (!strncmp(command,"scene ",6)) { wanted_room=atoi(command+6); puts("ok"); }
        else if (!strncmp(command,"flag ",5)) { sst_net_send_flag(1,321,(uint8_t)atoi(command+5)); puts("ok"); }
        else if (!strncmp(command,"reward",6)) { SstChestReward r={.index=831,.item=176}; sst_net_send_chest(&r); puts("ok"); }
        else if (!strncmp(command,"damage",6)) {
            static uint32_t sequence;
            SstEnemyEvent e={.scene={.stage="D000"},.key=sst_world_enemy_key(0x16d,15,123),
                .sequence=++sequence,.profile=0x16d,.damage=1,.health=3};
            e.scene.room=(uint8_t)(int)wanted_room; sst_net_send_enemy(&e); puts("ok");
        }
        else if (!strncmp(command,"pause ",6)) { paused=atoi(command+6)!=0; puts("ok"); }
        else if (!strncmp(command,"actors ",7)) {
            int count=atoi(command+7); wanted_actors=count>0 && count<=SST_ENEMIES ? count : 1; puts("ok");
        }
        else if (!strncmp(command,"backlog",7)) {
            for (unsigned i=0;i<1000000;i++) sst_net_send_flag(1,321,1);
            puts("ok");
        }
        else if (!strncmp(command,"quit",4)) break;
        else { fprintf(stderr,"Unknown command: %s",command); break; }
    }
    alive=false; pthread_join(worker,NULL); sst_net_stop(); return 0;
}

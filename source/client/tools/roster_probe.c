// Asset-free actual native receiver + prop roster. Controlled only by owned stdin.
// Does not read game memory, saves, keys, assets or start any game/model code.
#include "../src/sst_net.h"
#include "../src/sst_props.h"
#include "../src/sst_world.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const SstScene area={.stage="F001r",.room=0,.layer=1,.entrance=0};
static SstPropState state;
static unsigned delivered,rejected;
static void receive_prop(const SstProp *p,void *unused) {
    (void)unused;
    if(sst_props_update(&state,p->from,p->presence_generation,p,sst_now_ms()))delivered++;
    else rejected++;
}
static void pose(uint32_t tick) {
    SstPose p={.tick=tick,.pos={100,200,300},.health=24,.max_health=24,.equip_flags=1};
    sst_net_send_pose(&p);
}
static void sample(void) {
    SstPlayer peers[SST_MAX_PLAYERS];int count=sst_net_players(peers,SST_MAX_PLAYERS);
    uint16_t mine=sst_net_my_id();uint32_t generation=sst_net_generation();
    bool online=sst_net_status(NULL,0)==SST_ONLINE && mine;
    SstPropPresence live[SST_MAX_PLAYERS];unsigned n=0;
    if(online) {
        sst_props_begin(&state,&area,generation);live[n++]=(SstPropPresence){mine,generation};
        for(int i=0;i<count;i++)if(peers[i].active && peers[i].id!=mine && sst_world_same_area(&peers[i].scene,&area)) {
            assert(n<SST_MAX_PLAYERS);live[n++]=(SstPropPresence){peers[i].id,peers[i].presence_generation};
        }
        assert(sst_props_roster(&state,live,n));sst_net_poll_props(receive_prop,NULL);
    } else {
        sst_props_reset(&state);sst_net_poll_props(NULL,NULL);
    }
    printf("{\"online\":%s,\"id\":%u,\"generation\":%u,\"peer_count\":%d,\"roster_count\":%u,\"delivered\":%u,\"rejected\":%u,\"peers\":[",
        online?"true":"false",mine,generation,count,n,delivered,rejected);
    for(int i=0;i<count;i++)printf("%s%u",i?",":"",peers[i].id);
    printf("],\"targets\":[");
    for(unsigned i=0;i<3;i++) {
        SstPropKey key={.placed_id=(uint16_t)(9+i),.profile=SST_PROP_BIG_PROFILE,.params=0,.room=0,.layer=1};
        SstProp out;
        if(i)putchar(',');
        if(sst_props_get(&state,&key,sst_now_ms(),&out))
            printf("{\"from\":%u,\"sequence\":%u,\"presence\":%u,\"phase\":%u}",out.from,out.sequence,out.presence_generation,out.phase);
        else printf("null");
    }
    puts("]}");fflush(stdout);
}
int main(int argc,char **argv) {
    if(argc!=3 || !argv[2][0] || strlen(argv[2])>=SST_STR)return 2;
    char *end=NULL;unsigned long port=strtoul(argv[1],&end,10);
    if(!argv[1][0] || *end || !port || port>65535)return 2;
    SstConfig config={.port=(uint16_t)port,.auto_reconnect=false,.use_udp=false};
    strcpy(config.host,"127.0.0.1");strcpy(config.name,"Native roster probe");strcpy(config.room,argv[2]);
    sst_net_start(&config);uint64_t deadline=sst_now_ms()+5000;
    while(sst_net_status(NULL,0)!=SST_ONLINE) {
        if(sst_net_status(NULL,0)==SST_FAILED || sst_now_ms()>=deadline){sst_net_stop();return 3;}
        struct timespec delay={.tv_nsec=10000000};nanosleep(&delay,NULL);
    }
    sst_net_send_scene(&area);pose(100);
    printf("{\"ready\":true,\"id\":%u,\"fixture\":\"actual-native-client-roster\"}\n",sst_net_my_id());fflush(stdout);
    char line[128];
    while(fgets(line,sizeof line,stdin)) {
        unsigned tick;char extra;
        if(!strcmp(line,"sample\n"))sample();
        else if(sscanf(line,"pose %u %c",&tick,&extra)==1){pose(tick);puts("{\"pose_queued\":true}");fflush(stdout);}
        else if(!strcmp(line,"quit\n"))break;
        else {puts("{\"error\":\"invalid_fixture_command\"}");fflush(stdout);}
    }
    sst_net_stop();return 0;
}

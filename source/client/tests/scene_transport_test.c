// Actual embedded host, TCP receiver, prop state and movement interpolation.
// A raw source is asset-free; no game process, assets or guest memory.
#define main existing_rejoin_fixture_main
#include "rejoin_test.c"
#undef main
#include "../src/sst_props.h"

static void send_scene(int fd, const SstScene *s) {
    unsigned char body[32];unsigned n=0;
    str(body,&n,s->stage);body[n++]=s->room;body[n++]=s->layer;body[n++]=s->entrance;
    send_frame(fd,0x10,body,n);
}
static SstPlayer observation(uint32_t previous, uint32_t tick, float x) {
    uint64_t deadline=sst_now_ms()+3000;
    do {
        SstPlayer peers[SST_MAX_PLAYERS];int n=sst_net_players(peers,SST_MAX_PLAYERS);
        for(int i=0;i<n;i++)if(!strcmp(peers[i].name,"Scene source") && peers[i].has_pose &&
            peers[i].presence_generation!=previous && peers[i].pose.tick==tick && peers[i].pose.pos[0]==x)
            return peers[i];
        usleep(1000);
    } while(sst_now_ms()<deadline);
    assert(!"TCP scene boundary did not start a fresh observation stream");
    return (SstPlayer){0};
}
static void bind_roster(SstPropState *state, const SstPlayer *peer) {
    SstPropPresence live[2]={{sst_net_my_id(),sst_net_generation()},{peer->id,peer->presence_generation}};
    assert(sst_props_roster(state,live,2));
}
static unsigned delivered;
static void receive_prop(const SstProp *p, void *state) {
    assert(sst_props_update(state,p->from,p->presence_generation,p,sst_now_ms()));delivered++;
}
static void wait_prop(SstPropState *state) {
    unsigned previous=delivered;uint64_t deadline=sst_now_ms()+2000;
    do {
        sst_net_poll_props(receive_prop,state);
        if(delivered==previous+1)return;
        usleep(1000);
    } while(sst_now_ms()<deadline);
    assert(!"TCP prop was not accepted in its new incarnation");
}
static void prop(int fd, uint16_t profile, uint32_t sequence, uint32_t tick, SstPropPhase phase, float x) {
    float y=phase==SST_PROP_CARRY?101.5f:1.5f;
    SstProp p={.scene=area,.key={.placed_id=9,.profile=profile,.params=0xfffffff0,.room=1,.layer=1},
        .sequence=sequence,.pose_tick=tick,.phase=phase,.origin={x,1.5f,-2700},.pos={x,y,-2700},
        .matrix={1,0,0,x,0,1,0,y,0,0,1,-2700}};
    uint8_t body[SST_PROP_WIRE_MAX];size_t n=sst_prop_encode(&p,body,sizeof body);assert(n);
    send_frame(fd,0x34,body,(unsigned)n);
}
int main(int argc, char **argv) {
    unsigned seconds=0;
    assert(argc==1 || (argc==3 && !strcmp(argv[1],"--soak-seconds")));
    if(argc==3) {
        char *end;unsigned long value=strtoul(argv[2],&end,10);
        assert(*argv[2] && !*end && value>=1 && value<=3600);seconds=(unsigned)value;
    }
    char why[160];assert(sst_host_start(0,"coop",why,sizeof why));
    unsigned id;int fd=join(sst_host_port(),"Scene source",&id);
    scene(fd);pose(fd,10,100);
    SstConfig config={.host="127.0.0.1",.port=sst_host_port(),.room="coop",.team="team",.name="Scene observer"};
    sst_net_start(&config);sst_net_send_scene(&area);
    SstPlayer current=wait_player("Scene source",1,10);
    unsigned cases=0;uint32_t connection=sst_net_generation();
    uint64_t started=sst_now_ms(),deadline=started+(uint64_t)seconds*1000;
    do {
     for(unsigned mode=1;mode<=3;mode++)for(unsigned i=0;i<3;i++)for(unsigned phase=1;phase<=2;phase++) {
        const uint16_t profiles[]={SST_PROP_PROFILE,SST_PROP_BIG_PROFILE,SST_PROP_BARREL_PROFILE};
        float old_x=1000+(cases%97)*200,new_x=old_x+100;
        scene(fd);pose(fd,100,old_x);
        current=observation(current.presence_generation,100,old_x);
        SstPropState state={0};sst_props_begin(&state,&area,1);bind_roster(&state,&current);
        sst_remote_reset();SstRemote shown[1];
        assert(sst_remote_network_sample(&area,shown,1)==1 && shown[0].pose.pos[0]==old_x);
        prop(fd,profiles[i],99,100,(SstPropPhase)phase,old_x);wait_prop(&state);
        SstPropKey key={.placed_id=9,.profile=profiles[i],.params=0xfffffff0,.room=1,.layer=1};SstProp out;
        assert(sst_props_get(&state,&key,sst_now_ms(),&out) && out.sequence==99);
        // No bridge or renderer update until every boundary and new pose arrive.
        if(mode==2) {SstScene away=area;strcpy(away.stage,"F002");away.room=4;send_scene(fd,&away);}
        SstScene next=area;if(mode==3)next.entrance++;
        send_scene(fd,&next);pose(fd,1,new_x);
        SstPlayer after=observation(current.presence_generation,1,new_x);
        assert(after.id==id && sst_net_generation()==connection);
        bind_roster(&state,&after);
        assert(!sst_props_get(&state,&key,sst_now_ms(),&out));
        assert(sst_remote_network_sample(&area,shown,1)==1 && shown[0].pose.pos[0]==new_x && shown[0].pose.tick==1);
        prop(fd,profiles[i],1,1,(SstPropPhase)phase,new_x);wait_prop(&state);
        assert(sst_props_get(&state,&key,sst_now_ms(),&out) && out.sequence==1 &&
            out.presence_generation==after.presence_generation && out.pos[0]==new_x);
        current=after;cases++;
        if(!seconds)printf("TCP scene mode%u profile%u phase%u PASS\n",mode,profiles[i],phase);
     }
     if(seconds) {
        printf("TCP scene soak: %u cases, %llu ms, original connection retained\n",cases,(unsigned long long)(sst_now_ms()-started));
        fflush(stdout);
     }
    } while(seconds && sst_now_ms()<deadline);
    close(fd);sst_net_stop();sst_host_stop();sst_remote_reset();
    printf("TCP scene incarnation: %u cases PASS; host/receiver sockets closed, no game\n",cases);
    return 0;
}

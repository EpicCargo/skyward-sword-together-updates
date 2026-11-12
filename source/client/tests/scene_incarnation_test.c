// Actual receiver + prop ownership + remote playout; no sockets or game memory.
#include "../src/sst_net.c"
#include "../src/sst_remote.h"
#include <assert.h>
#include <math.h>

static const SstScene area = {.stage="F001r",.room=1,.layer=1,.entrance=5};
static unsigned checks, failures;
#define CHECK(label, condition) do { checks++; if (!(condition)) { failures++; fprintf(stderr,"FAIL %s\n",label); } } while (0)
static void scene_frame(uint16_t id, const SstScene *scene, bool malformed) {
    Buf b={0};put16(&b,id);put_scene(&b,scene);
    on_frame(T_SCENE,b.b,b.n-(malformed?1:0));
}
static void receive_pose(uint32_t tick, float x) {
    SstPose pose={.tick=tick,.pos={x,0,0},.health=24,.max_health=24};
    Buf b={0};put16(&b,2);put_pose(&b,&pose);on_frame(T_POSE,b.b,b.n);
}
static void roster(SstPropState *state) {
    SstPropPresence live[2]={{1,3},{2,players[0].presence_generation}};
    assert(sst_props_roster(state,live,2));
}
static void consume(const SstProp *p, void *state) {
    assert(sst_props_update(state,p->from,p->presence_generation,p,sst_now_ms()));
}
static void prop_frame(const SstProp *p) {
    Buf b={0};put16(&b,2);b.n+=sst_prop_encode(p,b.b+b.n,sizeof b.b-b.n);
    on_frame(T_PROPS_ALL,b.b,b.n);
}
static void check_case(unsigned mode, uint16_t profile, SstPropPhase phase) {
    memset(players,0,sizeof players);prop_n=0;my_id=1;presence_serial=10;
    atomic_store(&generation,3);status=SST_ONLINE;have_scene=true;my_scene=area;
    strcpy(cfg.team,"team");sock=udp_sock=SST_INVALID_SOCKET;cfg.use_udp=false;
    players[0]=(SstPlayer){.id=2,.active=true,.presence_generation=7,.scene=area};
    strcpy(players[0].team,"team");
    receive_pose(100,1000);
    sst_remote_reset();SstRemote shown[1];
    assert(sst_remote_sample(&area,players,1,players[0].pose_time_ms,shown,1)==1);
    assert(shown[0].pose.tick==100 && shown[0].pose.pos[0]==1000);
    SstPropState state={0};sst_props_begin(&state,&area,1);roster(&state);
    SstProp p={.scene=area,.key={.placed_id=9,.profile=profile,.params=0xfffffff0,.room=1,.layer=1},
        .sequence=99,.pose_tick=100,.phase=phase,.matrix={1,0,0,0,0,1,0,0,0,0,1,0}},out;
    prop_frame(&p);sst_net_poll_props(consume,&state);
    assert(sst_props_get(&state,&p.key,sst_now_ms(),&out));
    // Queue another old-incarnation packet too: both queued and accepted state
    // must retire even if the bridge never samples the intermediate departure.
    p.sequence=100;prop_frame(&p);assert(prop_n==1);
    bool changed=mode<=3 || mode==6;
    if(mode==1)scene_frame(2,&area,false); // same-room reload
    else if(mode==2) { // out and back before the next consumer update
        SstScene away=area;strcpy(away.stage,"F002");away.room=4;
        scene_frame(2,&away,false);scene_frame(2,&area,false);
    } else if(mode==3) {
        SstScene entrance=area;entrance.entrance++;scene_frame(2,&entrance,false);
    } else if(mode==4)scene_frame(2,&area,true); // malformed control
    else if(mode==5)scene_frame(77,&area,false); // unknown-peer control
    else if(mode==6) { // established JOIN replacement behavior must survive
        Buf b={0};put16(&b,2);putstr(&b,"returned");putstr(&b,"team");on_frame(T_PLAYER_JOIN,b.b,b.n);
        scene_frame(2,&area,false);
    } else assert(!"unknown scene incarnation mode");
    CHECK("scene retires only the affected authenticated incarnation",changed ? players[0].presence_generation!=7 : players[0].presence_generation==7);
    CHECK("scene drains only affected old queued props",prop_n==(changed?0:1));
    roster(&state);
    CHECK("accepted target cannot survive a scene incarnation",sst_props_get(&state,&p.key,sst_now_ms(),&out)!=changed);
    if(changed) {
        receive_pose(1,1100);
        CHECK("new observation tick accepted after scene",players[0].has_pose && players[0].pose.tick==1);
        int n=sst_remote_sample(&area,players,1,players[0].pose_time_ms,shown,1);
        CHECK("new scene does not interpolate through old pose history",n==1 && shown[0].pose.tick==1 && fabsf(shown[0].pose.pos[0]-1100)<.001f);
        p.sequence=1;p.pose_tick=1;prop_frame(&p);
        CHECK("new scene packet queued",prop_n==1);
        if(prop_n) {
            CHECK("new scene starts its own replay watermark",sst_props_update(&state,2,in_props[0].presence_generation,&in_props[0],sst_now_ms()));
            prop_n=0;
        }
        CHECK("new scene target retains authenticated incarnation",sst_props_get(&state,&p.key,sst_now_ms(),&out) && out.sequence==1 && out.presence_generation==players[0].presence_generation);
    } else {
        p.sequence=99;CHECK("control retains old replay watermark",!sst_props_update(&state,2,7,&p,sst_now_ms()));
    }
    printf("scene mode%u profile%u phase%u complete\n",mode,profile,phase);
    sst_remote_reset();
}
int main(int argc, char **argv) {
    assert(argc==1 || argc==4);
    if(argc==4)check_case((unsigned)strtoul(argv[1],NULL,10),(uint16_t)strtoul(argv[2],NULL,10),(SstPropPhase)strtoul(argv[3],NULL,10));
    else for(unsigned mode=1;mode<=6;mode++)for(unsigned i=0;i<3;i++)for(unsigned phase=1;phase<=2;phase++) {
        const uint16_t profiles[]={SST_PROP_PROFILE,SST_PROP_BIG_PROFILE,SST_PROP_BARREL_PROFILE};
        check_case(mode,profiles[i],(SstPropPhase)phase);
    }
    printf("scene-incarnation: %u checks, %u failures; actual receiver/state/playout, no game\n",checks,failures);
    return failures?1:0;
}

#include "../src/sst_teleport.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static SstScene area(const char *name,uint8_t room,uint8_t layer,uint8_t entrance) {
    SstScene s={.room=room,.layer=layer,.entrance=entrance};snprintf(s.stage,sizeof s.stage,"%s",name);return s;
}
static SstPlayer player(void) {
    SstPlayer p={.active=true,.id=2,.presence_generation=9,.has_pose=true,.pose_time_ms=1000,.udp_scene=77};
    p.scene=area("F001r",0,1,2);p.pose=(SstPose){.tick=3,.pos={10,20,-30},.rot={-10,2048,20},.health=24,.max_health=24,.anim_id=26,.anim_frame=4,.action=0};
    strcpy(p.team,"other-team");return p;
}
static SstTeleportDecision step(SstTeleport *s,SstPlayer *p,SstScene local,bool ready,uint32_t instance,uint64_t now) {
    return sst_teleport_step(s,p,5,&local,ready,instance,now);
}
static void same_area_without_recovery(void) {
    SstPlayer p=player();SstTeleport s={0};assert(sst_teleport_begin(&s,&p,5,1000));
    SstScene here=p.scene;here.entrance=99;SstTeleportDecision d=step(&s,&p,here,true,30,1050);
    assert(d.action==SST_TELEPORT_PLACE && d.nonce==s.nonce);
    assert(!memcmp(d.pose.pos,p.pose.pos,sizeof p.pose.pos) && !memcmp(d.pose.rot,p.pose.rot,sizeof p.pose.rot));
    assert(!s.loading && !d.scene.stage[0]); // no entrance reload or world snapshot
    assert(!sst_teleport_complete(&s,s.nonce+1));
    assert(sst_teleport_complete(&s,d.nonce));assert(!s.active && s.reason==SST_TELEPORT_REASON_DONE);
    assert(step(&s,&p,here,true,30,1060).action==SST_TELEPORT_IDLE);
    // Each placement decision follows the latest whole pose, not coordinates at begin.
    assert(sst_teleport_begin(&s,&p,5,1100));p.pose.pos[0]=70;p.pose.rot[1]=-2048;p.pose_time_ms=1110;
    d=step(&s,&p,here,true,30,1120);assert(d.action==SST_TELEPORT_PLACE && d.pose.pos[0]==70 && d.pose.rot[1]==-2048);
}
static void crossing_load_ack_and_fresh_arrival(void) {
    SstPlayer p=player();SstTeleport s={0};SstScene other=area("F000",0,0,0);
    p.has_pose=false;assert(sst_teleport_begin(&s,&p,5,1000));
    SstTeleportDecision d=step(&s,&p,other,false,30,1001);assert(d.action==SST_TELEPORT_WAIT);
    assert(!sst_teleport_load_accepted(&s,s.nonce,30));
    d=step(&s,&p,other,true,30,1002);assert(d.action==SST_TELEPORT_LOAD_AREA);
    assert(!memcmp(&d.scene,&p.scene,sizeof p.scene) && !s.loading);
    // A native load that declined stays retryable, rather than becoming a fake load.
    assert(step(&s,&p,other,true,30,1003).action==SST_TELEPORT_LOAD_AREA);
    assert(!sst_teleport_load_accepted(&s,s.nonce+1,30));assert(!sst_teleport_load_accepted(&s,s.nonce,0));
    assert(sst_teleport_load_accepted(&s,s.nonce,30));assert(!sst_teleport_load_accepted(&s,s.nonce,30));
    assert(step(&s,&p,other,true,30,1200).action==SST_TELEPORT_WAIT);
    // Global scene fields can change before the old local actor was replaced.
    assert(step(&s,&p,p.scene,true,30,1300).action==SST_TELEPORT_WAIT && !s.arrived);
    p.has_pose=true;p.pose_time_ms=1390;
    assert(step(&s,&p,p.scene,true,31,1400).action==SST_TELEPORT_WAIT && s.arrived);
    assert(step(&s,&p,p.scene,true,31,1410).reason==SST_TELEPORT_REASON_WAIT_POSE);
    p.pose_time_ms=1400;assert(step(&s,&p,p.scene,true,31,1411).action==SST_TELEPORT_WAIT);
    p.pose_time_ms=1412;p.pose.pos[2]=987;
    d=step(&s,&p,p.scene,true,31,1413);assert(d.action==SST_TELEPORT_PLACE && d.pose.pos[2]==987);
    assert(sst_teleport_complete(&s,d.nonce));
}
static void source_identity_and_timeouts(void) {
    SstPlayer p=player();SstTeleport s={0};SstScene here=p.scene;
    assert(sst_teleport_begin(&s,&p,5,1000));assert(sst_teleport_step(&s,&p,6,&here,true,1,1001).reason==SST_TELEPORT_REASON_CONNECTION);
    assert(sst_teleport_begin(&s,&p,5,1000));p.presence_generation++;assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_REJOINED);
    p=player();assert(sst_teleport_begin(&s,&p,5,1000));assert(sst_teleport_step(&s,NULL,5,&here,true,1,1001).reason==SST_TELEPORT_REASON_LEFT);
    p=player();assert(sst_teleport_begin(&s,&p,5,1000));p.active=false;assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_LEFT);
    p=player();assert(sst_teleport_begin(&s,&p,5,1000));p.id=3;assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_LEFT);
    p=player();assert(sst_teleport_begin(&s,&p,5,1000));p.scene.layer++;assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_TARGET_AREA);
    p=player();assert(sst_teleport_begin(&s,&p,5,1000));p.udp_scene++;assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_TARGET_INSTANCE);
    p=player();p.udp_scene=0;assert(sst_teleport_begin(&s,&p,5,1000));p.udp_scene=88;assert(step(&s,&p,here,true,1,1001).action==SST_TELEPORT_PLACE); // legacy/no epoch at capture
    p=player();assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,false,1,16001).reason==SST_TELEPORT_REASON_TIMEOUT);
    assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,999).reason==SST_TELEPORT_REASON_TIMEOUT);
    assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,false,1,1001).action==SST_TELEPORT_WAIT);assert(step(&s,&p,here,true,1,1000).reason==SST_TELEPORT_REASON_TIMEOUT);
}
static void invalid_and_busy_pose_guards(void) {
    SstPlayer p=player();SstTeleport s={0};SstScene here=p.scene;
    p.pose.health=0;assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_DEAD);
    p=player();p.pose.health=25;assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_INVALID);
    p=player();p.pose.max_health=81;assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_INVALID);
    const float bad[]={NAN,INFINITY,-INFINITY,1000001.f,-1000001.f};
    for(unsigned j=0;j<3;j++)for(unsigned i=0;i<sizeof bad/sizeof *bad;i++) {
        p=player();p.pose.pos[j]=bad[i];assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_INVALID);
    }
    p=player();p.pose.anim_frame=NAN;assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_INVALID);
    p=player();p.pose.equip_flags=0x80;assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_BOSS);
    p=player();p.pose.equip_flags=0x40;assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_WAIT_PLAYER);
    p.pose.equip_flags=0;p.pose.action=109;assert(step(&s,&p,here,true,1,1002).reason==SST_TELEPORT_REASON_WAIT_PLAYER);
    p.pose.action=3;assert(step(&s,&p,here,true,1,1003).action==SST_TELEPORT_PLACE);
    p=player();p.pose_time_ms=1002;assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,1001).reason==SST_TELEPORT_REASON_WAIT_POSE);
    p=player();assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,here,true,1,2000).action==SST_TELEPORT_PLACE);assert(step(&s,&p,here,true,1,2001).reason==SST_TELEPORT_REASON_WAIT_POSE);
    p.has_pose=false;assert(step(&s,&p,here,true,1,2002).action==SST_TELEPORT_WAIT);
}
static void local_reloads_wrong_layer_and_reset(void) {
    SstPlayer p=player();SstTeleport s={0};SstScene other=area("F009r",0,0,0);
    assert(sst_teleport_begin(&s,&p,5,1000));assert(step(&s,&p,other,true,30,1001).action==SST_TELEPORT_LOAD_AREA);assert(sst_teleport_load_accepted(&s,s.nonce,30));
    SstScene wrong=p.scene;wrong.layer++;assert(step(&s,&p,wrong,true,31,1400).action==SST_TELEPORT_WAIT);
    assert(!s.arrived);assert(step(&s,&p,wrong,true,31,46002).reason==SST_TELEPORT_REASON_TIMEOUT);
    assert(sst_teleport_begin(&s,&p,5,50000));assert(step(&s,&p,other,true,30,50001).action==SST_TELEPORT_LOAD_AREA);assert(sst_teleport_load_accepted(&s,s.nonce,30));
    p.pose_time_ms=51000;assert(step(&s,&p,p.scene,true,31,51000).action==SST_TELEPORT_WAIT);
    p.pose_time_ms=51001;assert(step(&s,&p,p.scene,true,32,51002).reason==SST_TELEPORT_REASON_LOCAL_INSTANCE);
    uint32_t old=s.nonce;assert(sst_teleport_begin(&s,&p,5,52000));assert(s.nonce!=old);assert(!sst_teleport_complete(&s,old));
    sst_teleport_cancel(&s,SST_TELEPORT_REASON_CANCELLED);assert(!s.active);assert(step(&s,&p,p.scene,true,32,52001).action==SST_TELEPORT_IDLE);
    sst_teleport_reset(&s);assert(!s.active && !s.nonce);assert(!sst_teleport_complete(&s,0));
    assert(!sst_teleport_begin(NULL,&p,5,0));assert(!sst_teleport_begin(&s,NULL,5,0));
    p=player();memset(p.scene.stage,'x',sizeof p.scene.stage);assert(!sst_teleport_begin(&s,&p,5,0));
    p=player();strcpy(p.scene.stage,"../../bad");assert(!sst_teleport_begin(&s,&p,5,0));
    p=player();strcpy(p.scene.stage,"F000\n");assert(!sst_teleport_begin(&s,&p,5,0));
    assert(sst_teleport_step(NULL,&p,5,&other,true,1,0).action==SST_TELEPORT_IDLE);
    for(unsigned i=0;i<=SST_TELEPORT_REASON_DONE;i++)assert(sst_teleport_reason_text((SstTeleportReason)i)[0]);
}
int main(void) {
    same_area_without_recovery();crossing_load_ack_and_fresh_arrival();source_identity_and_timeouts();
    invalid_and_busy_pose_guards();local_reloads_wrong_layer_and_reset();
    puts("peer teleport policy PASS: exact fresh pose, acknowledged load, new actor/arrival sample, cancellation guards; no state/progress requests");
    return 0;
}

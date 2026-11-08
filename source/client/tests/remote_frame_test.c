// No sockets/game/memory access. Link src/sst_remote.c + src/sst_pose_buffer.c.
// Exercise real interpolation and immutable carrier placement, not sampler counts.
#include "../src/sst_remote.h"
#include "../src/sst_props.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint64_t now_ms=1000;
static uint32_t generation=7;
static SstStatus status=SST_ONLINE;
static SstPlayer player;
static SstScene scene={.stage="F001r",.room=1,.layer=1,.entrance=3};
uint64_t sst_now_ms(void) { return now_ms; }
uint32_t sst_net_generation(void) { return generation; }
SstStatus sst_net_status(char *why,int size) { (void)why;(void)size;return status; }
int sst_net_players(SstPlayer *out,int capacity) {
    if(capacity<=0 || !player.active)return 0;
    out[0]=player;return 1;
}
bool sst_animation_clip(uint16_t clip,float *length,bool *loop) {
    (void)clip;if(length)*length=0;if(loop)*loop=false;return false;
}
static void observation(float x) {
    player.pose.tick++;
    player.pose.pos[0]=x;
    player.pose_time_ms=now_ms;
}
static void start(void) {
    player=(SstPlayer){.active=true,.id=2,.presence_generation=11,.has_pose=true,
        .scene=scene,.pose={.health=24,.max_health=24,.anim_id=26}};
    strcpy(player.name,"Carrier");
    observation(0);
    sst_remote_reset();
}
// Independent one-axis placement expectation for a carried object's fixed
// offset. The production bridge additionally applies yaw/matrix rotation.
static float carrier_position(const SstProp *prop,bool *aligned) {
    SstPose displayed;
    *aligned=sst_remote_displayed(&prop->scene,prop->from,prop->presence_generation,&displayed);
    return *aligned ? displayed.pos[0]+prop->pos[0]-prop->origin[0] : prop->pos[0];
}
static SstProp carried(void) {
    SstProp p={.scene=scene,.from=2,.presence_generation=player.presence_generation,.phase=SST_PROP_CARRY};
    p.origin[0]=player.pose.pos[0];p.pos[0]=p.origin[0]+12;
    return p;
}
static SstRemote one(void) {
    SstRemote out[SST_MAX_PLAYERS];
    assert(sst_remote_network_sample(&scene,out,SST_MAX_PLAYERS)==1);
    return out[0];
}
static void legacy_capture_free_interval(void) {
    start();
    for(unsigned i=0;i<80;i++) {
        now_ms+=34;observation((float)i*5);
        (void)one(); // old renderer/labels supplied the only refresh
    }
    bool aligned;SstProp p=carried();float warm=carrier_position(&p,&aligned);
    assert(aligned && p.pos[0]-warm>3 && p.pos[0]-warm<80);
    // Healthy new source observations alone do not refresh the display snapshot.
    now_ms+=300;observation(player.pose.pos[0]+45);p=carried();
    assert(carrier_position(&p,&aligned)==p.pos[0] && !aligned);
}
static void frame_driven_carrier(void) {
    start();
    for(uint64_t frame=0;frame<80;frame++) {
        now_ms+=34;observation((float)frame*5);
        sst_remote_frame(&scene,frame); // no body/label sampling at all
    }
    bool aligned;SstProp p=carried();float placed=carrier_position(&p,&aligned);
    SstRemote root=one();
    assert(aligned && fabsf(placed-(root.pose.pos[0]+12))<.001f);
    assert(p.pos[0]-placed>3 && p.pos[0]-placed<80);
    printf("carrier raw %.3f, interpolated %.3f, offset %.3f\n",p.pos[0],placed,placed-root.pose.pos[0]);
    // Further command lists, body draws and labels in this present all use the
    // same root, even when newer data/time arrives between their callbacks.
    now_ms+=5;observation(player.pose.pos[0]+5);
    sst_remote_frame(&scene,79);
    SstRemote duplicate=one();
    assert(duplicate.pose.pos[0]==root.pose.pos[0]);
    p=carried();assert(fabsf(carrier_position(&p,&aligned)-placed)<.001f && aligned);
    sst_remote_frame(&scene,80);
    assert(one().pose.pos[0]>root.pose.pos[0]);
    SstRemote out[SST_MAX_PLAYERS];
    assert(!sst_remote_network_sample(&scene,NULL,1));
    assert(!sst_remote_network_sample(&scene,out,0));
    assert(!sst_remote_network_sample(&scene,out,-1));
    assert(sst_remote_network_sample(&scene,out,1000000)==1);
    assert(!sst_remote_network_sample(NULL,out,1));
    // Serving a cached frame cannot extend its immutable display timestamp.
    now_ms+=251;
    sst_remote_frame(&scene,80); // repeated command list cannot refresh expiry
    assert(!sst_remote_network_sample(&scene,out,1));
    (void)carrier_position(&p,&aligned);assert(!aligned);
    // Nor can refreshing every render frame keep an expired SOURCE alive.
    now_ms+=1001;sst_remote_frame(&scene,81);
    assert(!sst_remote_network_sample(&scene,out,1));
    (void)carrier_position(&p,&aligned);assert(!aligned);
}
static void identities_and_resets(void) {
    start();uint64_t frame=200;sst_remote_frame(&scene,frame++);
    SstRemote out[SST_MAX_PLAYERS];SstPose pose;
    uint32_t old_presence=player.presence_generation;
    player.presence_generation++;player.pose.tick=0;observation(900);
    now_ms++;player.pose_time_ms=now_ms;sst_remote_frame(&scene,frame++);
    assert(one().pose.pos[0]==900);
    assert(!sst_remote_displayed(&scene,2,old_presence,&pose));
    assert(sst_remote_displayed(&scene,2,player.presence_generation,&pose));
    generation++;
    assert(!sst_remote_network_sample(&scene,out,1));
    assert(!sst_remote_displayed(&scene,2,player.presence_generation,&pose));
    sst_remote_frame(&scene,frame++);assert(one().pose.pos[0]==900);
    SstScene wrong=scene;wrong.room++;
    assert(!sst_remote_network_sample(&wrong,out,1));
    sst_remote_frame(&wrong,frame++);assert(!sst_remote_network_sample(&wrong,out,1));
    wrong=scene;wrong.layer++;sst_remote_frame(&wrong,frame++);
    assert(!sst_remote_network_sample(&wrong,out,1));
    sst_remote_frame(&scene,frame++);assert(one().pose.pos[0]==900);
    // Entrance is a frame-cache identity even though remote-area matching
    // intentionally groups entrances of the same stage/room/layer.
    wrong=scene;wrong.entrance++;
    assert(!sst_remote_network_sample(&wrong,out,1));
    sst_remote_frame(&wrong,frame++);assert(sst_remote_network_sample(&wrong,out,1)==1);
    assert(!sst_remote_network_sample(&scene,out,1));
    sst_remote_frame(NULL,frame++);
    assert(!sst_remote_network_sample(&wrong,out,1)); // no legacy resurrection
    sst_remote_frame(&scene,frame++);assert(one().pose.pos[0]==900);
    status=SST_OFFLINE;sst_remote_frame(&scene,frame++);
    assert(!sst_remote_network_sample(&scene,out,1));
    assert(!sst_remote_displayed(&scene,2,player.presence_generation,&pose));
    status=SST_ONLINE;sst_remote_frame(&scene,frame++);(void)one();
    // Renderer counter restart discards prior interpolation history.
    now_ms++;observation(950);sst_remote_frame(&scene,1);
    assert(one().pose.pos[0]==950);
    now_ms--;sst_remote_frame(&scene,2); // backwards clock; current source is future
    assert(!sst_remote_network_sample(&scene,out,1));
    observation(960);sst_remote_frame(&scene,3);assert(one().pose.pos[0]==960);
    sst_remote_reset();
    assert(!sst_remote_network_sample(&scene,out,1));
    assert(!sst_remote_displayed(&scene,2,player.presence_generation,&pose));
}
static void actual_carry_receipts(void) {
    start();uint64_t frame=600;
    player.pose.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
    player.pose.anim_id=20;player.pose.action=3;player.pose.anim_frame=10;
    sst_remote_frame(&scene,frame++);
    SstRemote source=one();SstPose shown,out;
    assert(source.presence_generation==player.presence_generation);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    // Mere sampling/preparation cannot authorize a native carried object.
    sst_remote_carry_drawn(&scene,&source,NULL);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    shown=source.pose;shown.anim_id=26;shown.action=67;shown.anim_frame=6;
    sst_remote_carry_drawn(&scene,&source,&shown);
    assert(sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    assert(out.anim_id==26 && out.action==67 && out.anim_frame==6 && out.pos[0]==source.pose.pos[0]);
    // Neutral fallback and a missing body revoke the previous carry immediately.
    shown.equip_flags=0;sst_remote_carry_drawn(&scene,&source,&shown);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    shown=source.pose;sst_remote_carry_drawn(&scene,&source,&shown);
    sst_remote_carry_drawn(&scene,&source,NULL);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    // A changed root cannot masquerade as the rendered source snapshot.
    shown.pos[0]++;sst_remote_carry_drawn(&scene,&source,&shown);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    shown=source.pose;sst_remote_carry_drawn(&scene,&source,&shown);
    SstRemote stale=source;stale.pose.tick--;
    sst_remote_carry_drawn(&scene,&stale,NULL);
    assert(sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    // Fresh network data cannot refresh a body which stopped drawing.
    now_ms+=251;observation(100);sst_remote_frame(&scene,frame++);source=one();
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    shown=source.pose;sst_remote_carry_drawn(&scene,&source,&shown);
    assert(sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    SstRemote old=source;
    player.presence_generation++;now_ms++;observation(100);sst_remote_frame(&scene,frame++);
    source=one();sst_remote_carry_drawn(&scene,&old,&old.pose);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    sst_remote_carry_drawn(&scene,&source,&source.pose);
    assert(sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    generation++;
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    sst_remote_frame(&scene,frame++);source=one();sst_remote_carry_drawn(&scene,&source,&source.pose);
    SstScene entrance=scene;entrance.entrance++;
    sst_remote_frame(&entrance,frame++);
    assert(!sst_remote_carry_displayed(&entrance,source.id,source.presence_generation,&out));
    sst_remote_carry_drawn(&scene,&source,&source.pose); // old local incarnation
    assert(!sst_remote_carry_displayed(&entrance,source.id,source.presence_generation,&out));
    sst_remote_frame(&scene,frame++);source=one();
    shown=source.pose;shown.equip_flags|=SST_POSE_CONTEXT_UNSUPPORTED;
    sst_remote_carry_drawn(&scene,&source,&shown);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    sst_remote_carry_drawn(&scene,&source,&source.pose);
    assert(sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    now_ms+=1001;sst_remote_frame(&scene,frame++);
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    status=SST_OFFLINE;
    assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
}
static void latest_source_revokes_delayed_carry(void) {
    for(unsigned invalid=0;invalid<3;invalid++) {
        status=SST_ONLINE;start();uint64_t frame=900+invalid*10;
        player.pose.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;player.pose.action=67;
        sst_remote_frame(&scene,frame++);SstRemote source=one();SstPose out;
        sst_remote_carry_drawn(&scene,&source,&source.pose);
        assert(sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
        now_ms+=34;
        if(invalid==0)player.pose.equip_flags=0;
        if(invalid==1)player.pose.equip_flags|=0x40;
        if(invalid==2)player.pose.health=0;
        observation(10);sst_remote_frame(&scene,frame++);source=one();
        // The delayed body still carries; the NEW authenticated source does not.
        assert(source.pose.health && (source.pose.equip_flags&SST_POSE_IDENTITY_MASK)==SST_POSE_CONTEXT_SMALL_CARRY);
        assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
        sst_remote_carry_drawn(&scene,&source,&source.pose);
        assert(!sst_remote_carry_displayed(&scene,source.id,source.presence_generation,&out));
    }
}
int main(void) {
    legacy_capture_free_interval();frame_driven_carrier();identities_and_resets();
    actual_carry_receipts();
    latest_source_revokes_delayed_carry();
    puts("remote frame: carrier stays interpolated without labels/body; frame reuse, TTL, source expiry and identities PASS");
    return 0;
}

// Actual animation service API; numeric palettes only, no game or network.
#include "../src/sst_animation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static SstScene scene;
static uint64_t frame;
static void reset(void) {
    sst_animation_reset();frame=0;
    scene=(SstScene){.stage="SYNTH",.room=1,.layer=1,.entrance=2};
}
static SstPose carry(void) {
    return (SstPose){.anim_id=26,.action=67,.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY};
}
static void step(SstPose *p,unsigned phase) {
    p->anim_frame=(float)phase;p->tick++;
    sst_animation_frame(&scene,p,++frame);
    assert(sst_animation_set_length(p->anim_id,45));
}
static bool part(unsigned mesh,SstPose *p,float y,bool rigid) {
    float matrix[12];memcpy(matrix,identity,sizeof matrix);matrix[7]=y;
    return sst_animation_capture((uint16_t)mesh,p,identity,rigid?matrix:identity,
        rigid?NULL:matrix,rigid?0:sizeof matrix,NULL,0);
}
static void parts(SstPose *p,unsigned phase) {
    (void)part(1,p,80+(float)phase,false);
    (void)part(2,p,100+(float)phase,false);
    (void)part(52,p,120+(float)phase,true);
    (void)part(53,p,140+(float)phase,true);
}
static SstPose warm(void) {
    SstPose p=carry();
    for(unsigned i=0;i<12;i++){step(&p,i);parts(&p,i);}
    step(&p,12);parts(&p,12);
    assert(sst_animation_replay_stats().carry_parts==4);
    return p;
}
static SstPose unseen(void) {
    SstPose p=carry();p.anim_id=20;p.action=3;p.anim_frame=30;
    p.pos[0]=200;p.pos[1]=20;p.pos[2]=-30;p.equip_flags|=0x21;
    return p;
}
static void selected(const SstAnimationReplay *replay,unsigned mesh,bool rigid,float y) {
    SstAnimationPose pose;float out[28]={0};
    assert(sst_animation_replay_select((uint16_t)mesh,replay,rigid?0:48,0,&pose));
    assert(sst_animation_uniform(&pose,&replay->pose,identity,rigid?0:3,out,rigid?sizeof out:48));
    assert(fabsf(out[rigid?23:7]-y)<.02f);
}
static void warm_fallback_and_exact_priority(void) {
    reset();SstPose local=warm(),requested=unseen();SstAnimationReplay replay;
    assert(sst_animation_replay_prepare(&requested,&replay) && replay.ready && replay.cohort);
    assert(replay.pose.anim_id==26 && replay.pose.action==67 && replay.pose.anim_frame==6);
    assert(replay.pose.equip_flags==requested.equip_flags);
    assert(!memcmp(replay.pose.pos,requested.pos,sizeof requested.pos));
    uint64_t cohort=replay.cohort,hash=sst_animation_teaching_hash();
    selected(&replay,1,false,106);selected(&replay,2,false,126);
    selected(&replay,52,true,146);selected(&replay,53,true,166);
    SstAnimationReplay corrupt=replay;SstAnimationPose rejected;
    corrupt.pose.equip_flags&=(uint8_t)~SST_POSE_CONTEXT_MASK;
    assert(!sst_animation_replay_select(1,&corrupt,48,0,&rejected));
    assert(sst_animation_teaching_hash()==hash); // LRU/replay is not teaching
    assert(sst_animation_replay_stats().carry_frames==1);
    // Exact carry retains requested native phase instead of static fallback.
    requested=local;requested.anim_frame=9;
    assert(sst_animation_replay_prepare(&requested,&replay) && !replay.cohort);
    selected(&replay,1,false,89);
    // Once a full carried gait is learned, it outranks the idle snapshot.
    local=unseen();
    for(unsigned i=0;i<12;i++){step(&local,i);parts(&local,50+i);}
    step(&local,12);parts(&local,62);requested=local;requested.anim_frame=9;
    assert(sst_animation_replay_prepare(&requested,&replay) && !replay.cohort);
    selected(&replay,1,false,139);
    requested=unseen();assert(sst_animation_replay_prepare(&requested,&replay) && replay.cohort==cohort);
}
static void mixed_and_incomplete_rejected(void) {
    reset();SstPose p=carry(),remote=unseen();SstAnimationReplay replay;
    for(unsigned i=0;i<=6;i++) {
        step(&p,i);(void)part(1,&p,80+i,false);
        SstPose other=p;if(i==6)other.tick++;
        (void)part(2,&other,100+i,false);
    }
    step(&p,7); // mixed tick cannot publish
    assert(!sst_animation_replay_stats().carry_parts);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    (void)part(1,&p,87,false);(void)part(2,&p,107,false);
    step(&p,8);assert(sst_animation_replay_stats().carry_parts==2);
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort);
    // A new required body layout/family has no carry snapshot: no partial fallback.
    assert(sst_animation_declare_part(9,96,0));step(&p,9);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    // A missing declared part never manufactures a complete replacement.
    (void)part(1,&p,89,false);(void)part(2,&p,109,false);
    assert(sst_animation_declare_part(9,96,0));step(&p,10);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    reset();p=carry();
    for(unsigned i=0;i<=8;i++) {
        step(&p,i);(void)part(1,&p,80+i,false);
        assert(sst_animation_declare_part(2,48,0));
    }
    step(&p,9);assert(!sst_animation_replay_stats().carry_parts);
    assert(!sst_animation_replay_prepare(&remote,&replay));
}
static void scope_and_unsafe(void) {
    const uint8_t flags[]={0,SST_POSE_CONTEXT_UNSUPPORTED,6,4|0x40,4|0x80};
    reset();SstPose p=warm(),remote=unseen();SstAnimationReplay replay;
    for(unsigned i=0;i<sizeof flags;i++) {
        remote.equip_flags=flags[i];assert(!sst_animation_replay_prepare(&remote,&replay));
    }
    remote=unseen();remote.anim_frame=NAN;assert(!sst_animation_replay_prepare(&remote,&replay));
    remote=unseen();scene.entrance++;step(&p,13);parts(&p,13);step(&p,14);
    assert(!sst_animation_replay_stats().carry_parts && !sst_animation_replay_prepare(&remote,&replay));
    reset();p=warm();scene.room++;step(&p,13);parts(&p,13);step(&p,14);
    assert(!sst_animation_replay_stats().carry_parts && !sst_animation_replay_prepare(&remote,&replay));
    reset();p=warm();scene.layer++;step(&p,13);parts(&p,13);step(&p,14);
    assert(!sst_animation_replay_stats().carry_parts && !sst_animation_replay_prepare(&remote,&replay));
    reset();p=warm();memcpy(scene.stage,"OTHER",6);step(&p,13);parts(&p,13);step(&p,14);
    assert(!sst_animation_replay_stats().carry_parts && !sst_animation_replay_prepare(&remote,&replay));
    reset();p=warm();sst_animation_frame(NULL,NULL,++frame);
    step(&p,13);parts(&p,13);step(&p,14);
    assert(!sst_animation_replay_stats().carry_parts && !sst_animation_replay_prepare(&remote,&replay));
    // Unsafe native observations may not seed the new fallback, even if the
    // old exact-cache policy accepts their numeric identity.
    reset();p=carry();p.equip_flags|=0x40;
    for(unsigned i=0;i<12;i++){step(&p,i);parts(&p,i);}
    step(&p,12);assert(!sst_animation_replay_stats().carry_parts);
    assert(!sst_animation_replay_prepare(&remote,&replay));
}
static void ordinary_neutral_preserved(void) {
    reset();SstPose p={.anim_id=26,.action=0},remote=unseen();SstAnimationReplay replay;
    for(unsigned i=0;i<12;i++){step(&p,i);parts(&p,i);}
    step(&p,12);parts(&p,12);
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort);
    assert(replay.pose.action==0 && !(replay.pose.equip_flags&SST_POSE_IDENTITY_MASK));
    assert(!sst_animation_replay_stats().carry_frames && !sst_animation_replay_stats().carry_parts);
    remote.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED;
    assert(sst_animation_replay_prepare(&remote,&replay) && !replay.pose.action);
}
static void eviction_fails_closed(void) {
    reset();SstPose p=warm(),remote=unseen();SstAnimationReplay replay;
    // The extra cohort shares the existing 8 MiB budget and ordinary LRU;
    // eviction of any required snapshot part must make it unusable.
    float palette[65520/sizeof(float)]={0};p.anim_id=20;p.action=3;
    for(unsigned i=0;i<6;i++)step(&p,i);
    for(unsigned i=0;i<180;i++) {
        step(&p,(6+i)%45);palette[3]=(float)i;
        assert(sst_animation_capture((uint16_t)(100+i),&p,identity,identity,
            palette,sizeof palette,palette,sizeof palette));
        assert(sst_animation_stats().bytes<=SST_ANIMATION_BUDGET);
        assert(sst_animation_stats().samples<=2048);
    }
    assert(!sst_animation_replay_stats().carry_parts);
    p.anim_id=168;p.action=2;step(&p,13);parts(&p,13);step(&p,14);
    assert(!sst_animation_replay_prepare(&remote,&replay));
}
static void reset_duration_and_denied_source(void) {
    reset();uint64_t cold=sst_animation_teaching_hash();SstPose p=warm(),remote=unseen();
    uint64_t warm_hash=sst_animation_teaching_hash();assert(cold!=warm_hash);
    SstAnimationReplay replay;
    // Geometry-only supply can retain an already verified snapshot but cannot
    // finalize or teach another one, and its stats/LRU do not change the hash.
    for(unsigned i=0;i<4;i++) {
        sst_animation_source_frame(&scene,&p,++frame,false);
        for(unsigned mesh=1;mesh<=2;mesh++)assert(sst_animation_declare_part((uint16_t)mesh,48,0));
        assert(sst_animation_declare_part(52,0,0));assert(sst_animation_declare_part(53,0,0));
        if(i)assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort);
        assert(sst_animation_teaching_hash()==warm_hash);
    }
    p.anim_frame=13;sst_animation_source_frame(&scene,&p,++frame,true);
    assert(sst_animation_set_length(26,46)); // discards all members safely
    assert(!sst_animation_replay_stats().carry_parts);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    reset();assert(sst_animation_teaching_hash()==cold);
    assert(!sst_animation_replay_stats().carry_frames && !sst_animation_replay_stats().carry_parts);
    assert(!sst_animation_replay_prepare(&remote,&replay));
}
int main(void) {
    warm_fallback_and_exact_priority();mixed_and_incomplete_rejected();scope_and_unsafe();
    ordinary_neutral_preserved();eviction_fails_closed();
    reset_duration_and_denied_source();sst_animation_reset();
    puts("carry cohort: exact priority, coherent native idle, complete manifest, scene/unsafe/lifetime gates PASS");
    return 0;
}

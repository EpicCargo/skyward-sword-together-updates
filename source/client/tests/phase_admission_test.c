// Public numeric API regressions; no game assets or native actor writes.
#include "../src/sst_animation.h"
#include "../src/sst_avatar.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static const SstScene scene={.stage="SYNTH",.room=1,.layer=1};
static uint64_t frame;
static void step(SstPose *p,float phase) {
    p->anim_frame=phase;p->tick++;
    sst_animation_frame(&scene,p,++frame);
}
static bool capture(const SstPose *p,float y) {
    float bone[12];memcpy(bone,identity,sizeof bone);bone[7]=y;
    return sst_animation_capture(6,p,identity,identity,bone,sizeof bone,NULL,0);
}
static bool selected_y(const SstPose *p,float *y) {
    SstAnimationPose selected;float bone[12];
    if(!sst_animation_select(6,p,48,0,&selected))return false;
    assert(sst_animation_uniform(&selected,p,identity,3,bone,48));
    *y=bone[7];return true;
}
static void reset(void) {sst_animation_reset();frame=0;}
static void source_loop_does_not_alias(unsigned context) {
    reset();SstPose p={.anim_id=20,.action=3,.equip_flags=(uint8_t)context};
    assert(sst_animation_set_length(20,45));
    for(unsigned phase=0;phase<=46;phase++)step(&p,(float)phase);
    bool admitted=capture(&p,80);SstPose peer=p;peer.anim_frame=1;float y=0;
    bool aliased=selected_y(&peer,&y);
    printf("context%u source46/length45 admitted=%d remote1_selected=%d y=%.3f\n",context,admitted,aliased,y);fflush(stdout);
    assert(!admitted && !aliased && sst_animation_stats().samples==0);
    // An invalid source cannot instantly resume capture: preserve native settling.
    for(unsigned phase=0;phase<=6;phase++) {
        step(&p,(float)phase);assert(capture(&p,10)==(phase==6));
    }
    peer.anim_frame=6;assert(selected_y(&peer,&y) && fabsf(y-10)<.01f);
    // Layout declaration and invalid source do not discard trustworthy samples.
    for(unsigned phase=7;phase<=45;phase++)step(&p,(float)phase);
    assert(capture(&p,12)); // exact endpoint is within the accepted native bound
    step(&p,46);assert(!capture(&p,90));
    peer.anim_frame=6;assert(selected_y(&peer,&y) && fabsf(y-10)<.01f);
}
static void finite_end_and_inline_source(void) {
    reset();SstPose p={.anim_id=197,.action=109};
    assert(sst_animation_set_length(197,12));
    for(unsigned phase=0;phase<=12;phase++)step(&p,(float)phase);
    assert(capture(&p,20));
    SstPose bad=p;bad.anim_frame=13;
    assert(!capture(&bad,80)); // even without another frame callback
    step(&p,13);assert(!capture(&p,90));
    SstPose peer=p;peer.anim_frame=12;float y;
    assert(selected_y(&peer,&y) && fabsf(y-20)<.01f);
    // Wrong caller-side phase remains rejected during uniform rebuilding too.
    SstAnimationPose selected;assert(sst_animation_select(6,&peer,48,0,&selected));
    float out[12];assert(!sst_animation_uniform(&selected,&bad,identity,3,out,48));
}
static void no_invalid_neutral(void) {
    reset();SstPose p={.anim_id=26,.action=0};
    assert(sst_animation_set_length(26,45));
    for(unsigned phase=0;phase<=46;phase++)step(&p,(float)phase);
    assert(!capture(&p,80));step(&p,47);
    SstPose unsupported={.anim_id=168,.action=2};SstAnimationReplay replay;
    assert(!sst_animation_replay_prepare(&unsupported,&replay));
    // A real in-range idle still supplies a coherent neutral fallback.
    for(unsigned phase=0;phase<=8;phase++) {
        step(&p,(float)phase);(void)capture(&p,10);
    }
    step(&p,9);assert(capture(&p,10));step(&p,10);
    assert(sst_animation_replay_prepare(&unsupported,&replay) && replay.ready && replay.cohort);
    assert(replay.pose.anim_frame<=45);
}
static void unknown_and_changed_duration(void) {
    reset();SstPose p={.anim_id=20,.action=3};
    for(unsigned phase=0;phase<=46;phase++)step(&p,(float)phase);
    assert(capture(&p,30)); // no guessed length before native metadata arrives
    assert(sst_animation_stats().samples==1);
    assert(sst_animation_set_length(20,45));
    assert(sst_animation_stats().samples==0); // existing length-change invalidation
    assert(!sst_animation_set_length(20,0));assert(!sst_animation_set_length(20,NAN));
    assert(!capture(&p,80));
    // A newly confirmed longer duration is valid; old samples stay invalidated.
    assert(sst_animation_set_length(20,60));
    for(unsigned phase=40;phase<=47;phase++)step(&p,(float)phase);
    assert(capture(&p,40));float y;assert(selected_y(&p,&y) && fabsf(y-40)<.01f);
}
static void invalid_local_cannot_bypass_rigid_policy(void) {
    reset();SstPose local={.anim_id=20,.action=3};
    assert(sst_animation_set_length(20,45));
    for(unsigned phase=0;phase<=45;phase++)step(&local,(float)phase);
    assert(capture(&local,10));step(&local,0);step(&local,1);assert(capture(&local,10));
    SstPose peer=local;assert(!sst_animation_skip_rigid(&local,&peer));
    local.anim_frame=46;assert(sst_animation_skip_rigid(&local,&peer));
    local=peer;local.pos[0]=NAN;assert(sst_animation_skip_rigid(&local,&peer));
    assert(sst_animation_skip_rigid(NULL,&peer));
}
int main(void) {
    for(unsigned i=0;i<4;i++)source_loop_does_not_alias((unsigned[]){0,2,4,6}[i]);
    finite_end_and_inline_source();no_invalid_neutral();unknown_and_changed_duration();invalid_local_cannot_bypass_rigid_policy();
    assert(sst_animation_stats().samples<=2048 && sst_animation_stats().bytes<=SST_ANIMATION_BUDGET);
    puts("phase admission: known source bounds, endpoints, settling, neutral, contexts and duration transitions PASS");
    sst_animation_reset();return 0;
}

// Public-API geometry-only provenance regressions. No game/network/native input.
#include "../src/sst_animation.h"
#include "../src/sst_avatar.h"
#include "../src/sst_hand.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const SstScene scene={.stage="F009r",.room=0,.layer=1};
static const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static float body[368*12],hand[12],result[368*12];
static uint64_t frame;
static uint64_t expected_animation_hash,expected_hand_hash;
static void matrices(void) {
    for(unsigned i=0;i<368;i++)memcpy(body+i*12,identity,sizeof identity);
    body[3*12+3]=10;body[3*12+7]=50;
    memcpy(hand,identity,sizeof hand);hand[3]=36.5f;hand[7]=50;
}
static SstPose local_pose(unsigned phase,uint16_t action) {
    return (SstPose){.anim_id=26,.anim_frame=(float)phase,.action=action,.tick=phase+1};
}
static void reset(void) {sst_animation_reset();frame=0;matrices();}
static void declare_body(void) {
    assert(sst_animation_declare_part(6,sizeof body,0));
    assert(sst_animation_declare_part(52,0,0));
}
static void capture_body(const SstPose *local,bool expect) {
    assert(sst_animation_capture(6,local,identity,identity,body,sizeof body,NULL,0)==expect);
    assert(sst_animation_capture(52,local,identity,hand,NULL,0,NULL,0)==expect);
}
static void remember_teaching(void) {
    expected_animation_hash=sst_animation_teaching_hash();
    expected_hand_hash=sst_hand_teaching_hash();
}
static void no_teaching(SstAnimationStats before,SstHandStats hands,size_t neutral_parts) {
    SstAnimationStats after=sst_animation_stats();SstHandStats now=sst_hand_stats();
    assert(sst_animation_teaching_hash()==expected_animation_hash);
    assert(sst_hand_teaching_hash()==expected_hand_hash);
    assert(after.bytes==before.bytes && after.samples==before.samples && after.captures==before.captures);
    assert(after.sword_captures==before.sword_captures && after.attachments==before.attachments);
    assert(now.entries==hands.entries && now.ready==hands.ready && now.blocked==hands.blocked);
    assert(now.blocked_parents==hands.blocked_parents && now.placements==hands.placements);
    assert(sst_animation_replay_stats().neutral_parts==neutral_parts);
    assert(after.bytes<=SST_ANIMATION_BUDGET && after.samples<=2048);
}
static void denied_capture(const SstPose *local) {
    // A denied source must return before touching model/view/palette pointers.
    const float *unread=(const float *)(uintptr_t)1;
    assert(!sst_animation_capture(6,local,unread,unread,unread,sizeof body,NULL,0));
    assert(!sst_animation_capture(52,local,unread,unread,NULL,0,NULL,0));
}
static void warm(void) {
    reset();
    for(unsigned phase=0;phase<12;phase++) {
        SstPose local=local_pose(phase,0);
        sst_animation_frame(&scene,&local,++frame);
        assert(sst_animation_set_length(26,45));
        capture_body(&local,phase>=6);
    }
    assert(sst_animation_replay_stats().neutral_parts==2);
    assert(sst_hand_stats().ready>0);
}
static void coherent_replay(void) {
    SstPose remote={.anim_id=168,.action=2,.pos={100,0,0}};
    SstAnimationReplay replay;SstAnimationPose selected;
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort && replay.pose.anim_id==26);
    assert(sst_animation_replay_select(6,&replay,sizeof body,0,&selected));
    assert(sst_animation_uniform(&selected,&replay.pose,identity,3,result,sizeof result));
    assert(fabsf(result[3*12+3]-110)<.003f && fabsf(result[3*12+7]-50)<.003f);
    assert(!sst_animation_replay_select(6,&replay,48,0,&selected));
}
static void retained_replay_without_learning(void) {
    warm();
    SstAnimationStats before=sst_animation_stats();SstHandStats hands=sst_hand_stats();
    size_t neutral=sst_animation_replay_stats().neutral_parts;
    remember_teaching();
    for(unsigned i=0;i<20;i++) {
        SstPose actual=local_pose(i,104);
        sst_animation_source_frame(&scene,&actual,++frame,false);
        declare_body();
        assert(!sst_animation_set_length(26,99)); // may not invalidate existing poses/hands
        float length=0;bool loop=false;assert(sst_animation_clip(26,&length,&loop) && length==45 && loop);
        denied_capture(&actual);
        coherent_replay();
        no_teaching(before,hands,neutral);
    }
    // Exit104 does not authorize an old forced command to teach the live idle.
    SstPose live_idle=local_pose(20,0);
    sst_animation_source_frame(&scene,&live_idle,++frame,false);
    declare_body();denied_capture(&live_idle);
    sst_animation_frame(&scene,&live_idle,frame); // later true in SAME present stays denied
    denied_capture(&live_idle);assert(!sst_animation_set_length(26,45));
    no_teaching(before,hands,neutral);
    // Declaration/replay mode does not widen final-palette identity or the
    // delivered known-length admission contract.
    const uint8_t contexts[]={0,2,4,6};SstAnimationPose selected;
    for(unsigned i=0;i<sizeof contexts;i++) {
        SstPose requested=local_pose(6,0);requested.equip_flags=contexts[i];
        assert(sst_animation_select(6,&requested,sizeof body,0,&selected)==(contexts[i]==0));
    }
    SstPose outside=local_pose(46,0);
    assert(!sst_animation_select(6,&outside,sizeof body,0,&selected));
    no_teaching(before,hands,neutral);
}
static void strict_declarations_and_overflow(void) {
    warm();SstPose actual=local_pose(12,104),unknown={.anim_id=168,.action=2};
    SstAnimationReplay replay;
    sst_animation_source_frame(&scene,&actual,++frame,false);
    declare_body();assert(sst_animation_declare_part(12,48,0)); // no captured cohort part12
    sst_animation_source_frame(&scene,&actual,++frame,false);
    assert(!sst_animation_replay_prepare(&unknown,&replay));
    declare_body();sst_animation_source_frame(&scene,&actual,++frame,false);coherent_replay();
    // A declaration is pure, but it must not weaken manifest bounds/coherence.
    assert(!sst_animation_declare_part(0,48,0));
    assert(!sst_animation_declare_part(55,0,0));assert(!sst_animation_declare_part(60,0,0));
    assert(!sst_animation_declare_part(6,1,0));assert(!sst_animation_declare_part(6,0,1));
    assert(!sst_animation_declare_part(6,SIZE_MAX,0));
    assert(!sst_animation_declare_part(6,0,SIZE_MAX));
    for(unsigned i=1;i<=128;i++)assert(sst_animation_declare_part(6,i*48u,0));
    assert(sst_animation_declare_part(6,48,0)); // duplicate does not consume capacity
    assert(!sst_animation_declare_part(6,129u*48u,0));
    sst_animation_source_frame(&scene,&actual,++frame,false);
    assert(!sst_animation_replay_prepare(&unknown,&replay));
    declare_body();sst_animation_source_frame(&scene,&actual,++frame,false);coherent_replay();
}
static void cold_geometry_never_settles_or_seeds_neutral(void) {
    reset();SstPose unknown={.anim_id=168,.action=2};SstAnimationReplay replay;
    SstAnimationStats before=sst_animation_stats();SstHandStats hands=sst_hand_stats();
    remember_teaching();
    for(unsigned i=0;i<20;i++) {
        SstPose actual=local_pose(i,104);
        sst_animation_source_frame(&scene,&actual,++frame,false);declare_body();denied_capture(&actual);
        assert(!sst_animation_replay_prepare(&unknown,&replay));
        no_teaching(before,hands,0);
    }
    // Forced phase advancement must NOT count toward natural settling. Fresh
    // ordinary observations need six new native phase advances before capture.
    for(unsigned i=0;i<=6;i++) {
        SstPose local=local_pose(20+i,0);
        sst_animation_frame(&scene,&local,++frame);assert(sst_animation_set_length(26,45));
        capture_body(&local,i==6);
        if(i<6)assert(!sst_animation_stats().samples);
    }
    SstPose next=local_pose(27,0);sst_animation_frame(&scene,&next,++frame);
    assert(sst_animation_replay_prepare(&unknown,&replay) && replay.cohort && replay.pose.anim_frame==26);
}
static void mixed_frame_discards_unfinished_teaching(void) {
    reset();SstPose local={0};
    for(unsigned i=0;i<=6;i++) {
        local=local_pose(i,0);sst_animation_frame(&scene,&local,++frame);
        assert(sst_animation_set_length(26,45));capture_body(&local,i==6);
    }
    // Natural phase6 has copied legitimate samples but its neutral cohort and
    // child-hand observation are still unfinished until the NEXT frame.
    assert(sst_animation_stats().samples==2);
    assert(!sst_animation_replay_stats().neutral_parts && !sst_hand_stats().entries);
    SstAnimationStats before=sst_animation_stats();SstHandStats hands=sst_hand_stats();
    remember_teaching();
    SstPose forced=local;forced.action=104;
    sst_animation_source_frame(&scene,&forced,frame,false);declare_body();
    sst_animation_frame(&scene,&local,frame);denied_capture(&local);
    no_teaching(before,hands,0);
    forced=local_pose(7,104);sst_animation_source_frame(&scene,&forced,++frame,false);declare_body();
    no_teaching(before,hands,0); // no delayed neutral publication or hand observation
    local=local_pose(8,0);sst_animation_frame(&scene,&local,++frame);
    no_teaching(before,hands,0);
    // A late forced-origin command labelled with live ordinary metadata must
    // deny this new frame too, including subsequent legacy frame() calls.
    sst_animation_source_frame(&scene,&local,frame,false);declare_body();
    sst_animation_frame(&scene,&local,frame);denied_capture(&local);
    local=local_pose(9,0);sst_animation_frame(&scene,&local,++frame);
    no_teaching(before,hands,0);
    // Previously valid NATURAL samples survive aborting unfinished learning.
    SstPose exact=local_pose(6,0);SstAnimationReplay replay;SstAnimationPose selected;
    assert(sst_animation_replay_prepare(&exact,&replay) && !replay.cohort);
    assert(sst_animation_replay_select(6,&replay,sizeof body,0,&selected));
    assert(sst_animation_uniform(&selected,&replay.pose,identity,3,result,sizeof result));
    assert(fabsf(result[3*12+3]-10)<.003f);
    SstPose unknown={.anim_id=168,.action=2};assert(!sst_animation_replay_prepare(&unknown,&replay));
}
static void teaching_hash_sensitivity(void) {
    reset();uint64_t cold_animation=sst_animation_teaching_hash(),cold_hand=sst_hand_teaching_hash();
    warm();uint64_t warm_animation=sst_animation_teaching_hash(),warm_hand=sst_hand_teaching_hash();
    assert(warm_animation!=cold_animation && warm_hand!=cold_hand);
    coherent_replay();sst_hand_note_placement(); // replay LRU and placements are not teaching
    assert(warm_animation==sst_animation_teaching_hash() && warm_hand==sst_hand_teaching_hash());
    assert(sst_animation_set_length(26,46)); // invalidates this learned clip and hands
    assert(warm_animation!=sst_animation_teaching_hash() && warm_hand!=sst_hand_teaching_hash());
    reset();assert(cold_animation==sst_animation_teaching_hash() && cold_hand==sst_hand_teaching_hash());
}
int main(void) {
    teaching_hash_sensitivity();
    retained_replay_without_learning();strict_declarations_and_overflow();
    cold_geometry_never_settles_or_seeds_neutral();mixed_frame_discards_unfinished_teaching();
    sst_animation_reset();
    puts("geometry-only: strict retained replay, no source/hand/neutral teaching, sticky mixed-frame denial and fresh settling PASS");
    return 0;
}

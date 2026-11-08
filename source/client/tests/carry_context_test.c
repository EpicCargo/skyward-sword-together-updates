#include "../src/sst_animation.h"
#include "../src/sst_avatar.h"
#include "../src/sst_hand.h"
#include "../src/sst_game.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static const SstScene scene={.stage="SYNTH",.room=1,.layer=1};
static uint64_t frame;
static void begin(void) { sst_animation_reset();frame=0; }
static void step(SstPose *p,float phase) {
    p->anim_frame=phase;p->tick++;
    sst_animation_frame(&scene,p,++frame);assert(sst_animation_set_length(p->anim_id,45));
}
static void translated(float out[12],float y) { memcpy(out,identity,48);out[7]=y; }
static bool capture(unsigned mesh,const SstPose *p,float y,bool rigid) {
    float matrix[12];translated(matrix,y);
    bool result=sst_animation_capture((uint16_t)mesh,p,identity,rigid?matrix:identity,
                                      rigid?NULL:matrix,rigid?0:48,NULL,0);
    assert(sst_animation_stats().bytes<=SST_ANIMATION_BUDGET && sst_animation_stats().samples<=2048);
    return result;
}
static void learn(SstPose *p,float y,bool second) {
    for(unsigned i=0;i<13;i++) {
        step(p,(float)i);
        bool got=capture(1,p,y,false);if(i>=6)assert(got);
        if(second){got=capture(2,p,y+5,false);if(i>=6)assert(got);}
    }
}
static float selected_y(uint16_t mesh,const SstPose *p,bool rigid,SstAnimationPose *selected) {
    SstAnimationPose pose;
    assert(sst_animation_select(mesh,p,rigid?0:48,0,&pose));
    float out[28]={0};
    assert(sst_animation_uniform(&pose,p,identity,rigid?0:3,out,rigid?sizeof out:48));
    if(selected)*selected=pose;
    return out[rigid?16+7:7];
}
static void assert_separated(uint16_t clip,uint16_t action,bool carry_first) {
    begin();SstPose normal={.anim_id=clip,.action=action},carry=normal;
    carry.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
    if(carry_first){learn(&carry,80,false);learn(&normal,10,false);}
    else {learn(&normal,10,false);learn(&carry,80,false);}
    normal.anim_frame=carry.anim_frame=9;
    float down=selected_y(1,&normal,false,NULL),up=selected_y(1,&carry,false,NULL);
    printf("context regression clip=%u action=%u order=%s normal_y=%.3f carry_y=%.3f\n",
           clip,action,carry_first?"carry-first":"normal-first",down,up);fflush(stdout);
    // Old code aliases these exact clip/action/phase/layout keys and fails here.
    assert(fabsf(down-10)<.001f && fabsf(up-80)<.001f);
    SstAnimationPose normal_sample,carry_sample;
    selected_y(1,&normal,false,&normal_sample);selected_y(1,&carry,false,&carry_sample);
    float out[12],unchanged[12];for(unsigned i=0;i<12;i++)out[i]=unchanged[i]=123;
    assert(!sst_animation_uniform(&normal_sample,&carry,identity,3,out,48));
    assert(!memcmp(out,unchanged,48));
    assert(!sst_animation_uniform(&carry_sample,&normal,identity,3,out,48));
    assert(!memcmp(out,unchanged,48));
    SstAnimationPose mixed=normal_sample;mixed.after=carry_sample.after;
    assert(!sst_animation_uniform(&mixed,&normal,identity,3,out,48));
    // Ownership/world/motion flags do not identify a different palette context.
    // DRAWN is tested independently by drawn_context_test.
    normal.equip_flags=0xe1;carry.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY|0xe1;
    assert(fabsf(selected_y(1,&normal,false,NULL)-10)<.001f);
    assert(fabsf(selected_y(1,&carry,false,NULL)-80)<.001f);
    sst_animation_reset();
}
static void context_settling(void) {
    begin();SstPose local={.anim_id=20,.action=3};learn(&local,10,false);
    step(&local,13);assert(capture(1,&local,10,false));
    local.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
    step(&local,13);assert(!capture(1,&local,80,false)); // context ONLY changed
    // No static-phase exception is added, regardless of distinct ticks/presents.
    for(unsigned i=0;i<8;i++){step(&local,13);assert(!capture(1,&local,80,false));}
    for(unsigned i=1;i<=6;i++) {
        local.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY|(i%2?0x81:0x21);
        step(&local,13+(float)i);
        assert(capture(1,&local,80,false)==(i==6));
    }
    local.equip_flags=0;step(&local,20);assert(!capture(1,&local,10,false));
    for(unsigned i=1;i<=6;i++){step(&local,20+(float)i);assert(capture(1,&local,10,false)==(i==6));}
    local.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED;
    for(unsigned i=0;i<9;i++){step(&local,27+(float)i);assert(!capture(1,&local,99,false));}
    SstAnimationPose pose;assert(!sst_animation_select(1,&local,48,0,&pose));
    sst_animation_reset();
}
static void neutral_is_context_zero(void) {
    begin();SstPose idle={.anim_id=26,.action=0};learn(&idle,10,true);
    SstPose carry={.anim_id=20,.action=3,.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY};
    learn(&carry,80,false); // mesh2 has no carry sample: whole body must use idle0
    step(&carry,13);assert(capture(1,&carry,80,false));
    SstPose untrusted=carry;untrusted.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED;
    // Geometry is still present even when its pose is not learnable. The
    // ordinary capture API retains that layout in the native body manifest.
    assert(!capture(2,&untrusted,85,false));step(&carry,14);
    SstPose unknown=carry;unknown.anim_frame=9;unknown.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED|0xe3;
    unknown.pos[0]=100;unknown.pos[1]=20;unknown.pos[2]=-30;
    SstAnimationPose pose;assert(!sst_animation_select(1,&unknown,48,0,&pose));
    SstAnimationReplay replay;assert(sst_animation_replay_prepare(&unknown,&replay));
    assert(replay.ready && replay.cohort && replay.pose.anim_id==26 && !replay.pose.action);
    assert(!(replay.pose.equip_flags&SST_POSE_IDENTITY_MASK) && replay.pose.equip_flags==0xe1);
    assert(!memcmp(replay.pose.pos,unknown.pos,sizeof unknown.pos));
    uint64_t cohort=replay.cohort;
    for(unsigned mesh=1;mesh<=2;mesh++) {
        assert(sst_animation_replay_select((uint16_t)mesh,&replay,48,0,&pose));
        float out[12];assert(sst_animation_uniform(&pose,&replay.pose,identity,3,out,48));
        assert(fabsf(out[7]-(20+10+5*(float)(mesh-1)))<.001f);
    }
    carry.anim_frame=9;assert(sst_animation_replay_prepare(&carry,&replay));
    assert(replay.cohort==cohort && !(replay.pose.equip_flags&SST_POSE_CONTEXT_MASK));
    SstAnimationReplay corrupt=replay;corrupt.pose.equip_flags|=SST_POSE_CONTEXT_SMALL_CARRY;
    assert(!sst_animation_replay_select(1,&corrupt,48,0,&pose));
    // Even idle26/action0 with context4 must never seed a neutral cohort.
    begin();idle.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;learn(&idle,80,true);
    step(&idle,13);assert(capture(1,&idle,80,false));assert(capture(2,&idle,85,false));
    assert(!sst_animation_replay_stats().neutral_parts);
    unknown.anim_id=168;unknown.action=2;
    assert(!sst_animation_replay_prepare(&unknown,&replay));
    sst_animation_reset();
}
static void normal_attachment_not_poisoned(void) {
    begin();SstPose normal={.anim_id=20,.action=3};
    for(unsigned i=0;i<13;i++) {
        step(&normal,(float)i);capture(6,&normal,10,false);capture(10,&normal,10,true);
    }
    step(&normal,13);assert(sst_animation_stats().attachments);
    SstPose carry=normal;carry.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
    for(unsigned i=0;i<=20;i++) {
        step(&carry,(float)i);capture(6,&carry,100,false);capture(10,&carry,300,true);
    }
    for(unsigned i=12;i<=18;i++)step(&normal,(float)i);
    assert(capture(6,&normal,10,false)); // validate against stored carry rigid phase18
    assert(sst_animation_stats().attachments);
    SstAnimationPose pose;float normal_y=selected_y(10,&normal,true,&pose);
    assert(pose.attachment && !pose.child && fabsf(normal_y-10)<.001f);
    carry.anim_frame=18;
    assert(fabsf(selected_y(10,&carry,true,&pose)-300)<.001f && !pose.attachment);
    // A carry body exists at30 but no direct carry rigid pose is near it.
    step(&carry,24);assert(!capture(6,&carry,100,false));
    step(&carry,30);assert(capture(6,&carry,100,false));
    assert(!sst_animation_select(10,&carry,0,0,&pose));
    sst_animation_reset();
}
static void normal_child_not_poisoned(void) {
    begin();static float body[368*12];
    for(unsigned j=0;j<368;j++)memcpy(body+j*12,identity,48);
    body[3*12+7]=50;
    SstPose normal={.anim_id=20,.action=3};
    for(unsigned i=0;i<=18;i++)step(&normal,(float)i);
    assert(sst_animation_capture(6,&normal,identity,identity,body,sizeof body,NULL,0));
    float offset[12],hand[12];memcpy(offset,identity,48);offset[3]=26.5f;
    assert(sst_hand_compose(body+3*12,offset,hand));
    sst_hand_observe(52,&normal,6,sizeof body,0,body+3*12,hand);
    normal.tick++;sst_hand_observe(52,&normal,6,sizeof body,0,body+3*12,hand);
    SstHandOffset got;assert(sst_hand_lookup(52,&normal,true,45,&got));
    SstPose carry=normal;carry.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
    float bad_hand[12];memcpy(bad_hand,hand,48);bad_hand[3]+=100;
    for(unsigned i=0;i<=20;i++) {
        step(&carry,(float)i);
        sst_animation_capture(6,&carry,identity,identity,body,sizeof body,NULL,0);
        sst_animation_capture(52,&carry,identity,bad_hand,NULL,0,NULL,0);
    }
    for(unsigned i=12;i<=18;i++)step(&normal,(float)i);
    assert(sst_animation_capture(6,&normal,identity,identity,body,sizeof body,NULL,0));
    step(&normal,19);normal.anim_frame=18; // promote the normal donor's current manifest
    assert(sst_hand_lookup(52,&normal,true,45,&got));
    assert(!sst_hand_stats().blocked && !sst_hand_stats().blocked_parents);
    SstAnimationPose selected;float y=selected_y(52,&normal,true,&selected);
    assert(selected.attachment && selected.child && fabsf(y-50)<.001f);
    carry.anim_frame=18;
    assert(sst_animation_select(52,&carry,0,0,&selected) && !selected.attachment);
    step(&carry,24);step(&carry,30);
    assert(sst_animation_capture(6,&carry,identity,identity,body,sizeof body,NULL,0));
    assert(!sst_animation_select(52,&carry,0,0,&selected));
    sst_animation_reset();
}
static void child_offset(uint8_t context,unsigned mesh,float out[12]) {
    memcpy(out,identity,48);
    float angle=context?.4f:-.1f;
    out[0]=out[5]=cosf(angle);out[1]=-sinf(angle);out[4]=sinf(angle);
    out[3]=context?(mesh==52?30.5f:32.5f):26.5f;
    out[7]=context?2.f:0;out[11]=context?-1.f:0;
}
static void matrix_near(const float *a,const float *b) {
    for(unsigned i=0;i<12;i++)assert(fabsf(a[i]-b[i])<.02f);
}
static void child_context_entries_and_parents(void) {
    begin();float joint[12],normal_local[12],carry_local[12],normal_model[12],carry_model[12];
    translated(joint,50);child_offset(0,52,normal_local);
    child_offset(SST_POSE_CONTEXT_SMALL_CARRY,52,carry_local);
    assert(sst_hand_compose(joint,normal_local,normal_model));
    assert(sst_hand_compose(joint,carry_local,carry_model));
    SstPose normal={.anim_id=20,.action=3,.anim_frame=9,.tick=100},carry=normal;
    carry.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
    SstHandOffset got;
    sst_hand_observe(52,&normal,6,368*48,0,joint,normal_model);
    assert(!sst_hand_lookup(52,&normal,true,45,&got));
    normal.tick++;sst_hand_observe(52,&normal,6,368*48,0,joint,normal_model);
    assert(sst_hand_lookup(52,&normal,true,45,&got));matrix_near(got.local,normal_local);
    assert(!sst_hand_lookup(52,&carry,true,45,&got)); // no normal-offset borrowing
    for(unsigned i=0;i<6;i++)sst_hand_observe(52,&carry,6,368*48,0,joint,carry_model);
    assert(!sst_hand_lookup(52,&carry,true,45,&got)); // same tick is still only one observation
    carry.tick++;sst_hand_observe(52,&carry,6,368*48,0,joint,carry_model);
    assert(sst_hand_lookup(52,&carry,true,45,&got));matrix_near(got.local,carry_local);
    float composed[12];assert(sst_hand_compose(joint,got.local,composed));matrix_near(composed,carry_model);
    normal.equip_flags=0xe1;carry.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY|0xe1;
    assert(sst_hand_lookup(52,&normal,true,45,&got));matrix_near(got.local,normal_local);
    assert(sst_hand_lookup(52,&carry,true,45,&got));matrix_near(got.local,carry_local);
    size_t entries=sst_hand_stats().entries;
    SstPose unknown=carry;unknown.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED;
    for(unsigned i=0;i<5;i++){unknown.tick++;sst_hand_observe(52,&unknown,6,368*48,0,joint,carry_model);}
    assert(!sst_hand_lookup(52,&unknown,true,45,&got) && sst_hand_stats().entries==entries);
    assert(!sst_hand_lookup(53,&carry,true,45,&got)); // unobserved side
    sst_hand_observe(53,&carry,6,367*48,0,joint,carry_model);assert(sst_hand_stats().entries==entries);
    // A contradictory carry parent blocks only the carry bank, never normal.
    float bad[12];memcpy(bad,carry_local,48);bad[3]+=1;
    assert(sst_hand_compose(joint,bad,carry_model));
    for(unsigned i=0;i<3;i++){carry.tick++;sst_hand_observe(52,&carry,6,368*48,0,joint,carry_model);}
    assert(!sst_hand_lookup(52,&carry,true,45,&got));
    assert(sst_hand_lookup(52,&normal,true,45,&got));matrix_near(got.local,normal_local);
    assert(sst_hand_stats().blocked_parents==1 && sst_hand_stats().entries<=512);
    sst_animation_reset();
}
static void child_body_context_required(void) {
    begin();static float body[368*12];
    for(unsigned i=0;i<368;i++)memcpy(body+i*12,identity,48);
    SstPose normal={.anim_id=20,.action=3},carry=normal;carry.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
    for(unsigned i=0;i<=10;i++) {
        step(&normal,(float)i);sst_animation_capture(6,&normal,identity,identity,body,sizeof body,NULL,0);
    }
    SstHandOffset got;float offset[12],hand[12];child_offset(SST_POSE_CONTEXT_SMALL_CARRY,52,offset);
    assert(sst_hand_compose(body+3*12,offset,hand));carry.anim_frame=9;carry.tick=100;
    for(unsigned i=11;i<=13;i++) {
        step(&normal,(float)i);
        assert(sst_animation_capture(6,&normal,identity,identity,body,sizeof body,NULL,0));
        SstPose mismatched=normal;mismatched.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY;
        assert(!sst_animation_capture(6,&mismatched,identity,identity,body,sizeof body,NULL,0));
        assert(!sst_animation_capture(52,&mismatched,identity,hand,NULL,0,NULL,0));
    }
    assert(!sst_hand_stats().entries); // mixed-context draws cannot seed child evidence
    sst_hand_observe(52,&carry,6,sizeof body,0,body+3*12,hand);
    carry.tick++;sst_hand_observe(52,&carry,6,sizeof body,0,body+3*12,hand);
    assert(sst_hand_lookup(52,&carry,true,45,&got));
    SstAnimationPose selected;
    assert(!sst_animation_select(52,&carry,0,0,&selected)); // only normal donor exists
    for(unsigned i=0;i<=10;i++) {
        step(&carry,(float)i);sst_animation_capture(6,&carry,identity,identity,body,sizeof body,NULL,0);
    }
    carry.anim_frame=9;
    assert(sst_animation_select(52,&carry,0,0,&selected) && selected.child && selected.attachment);
    normal.anim_frame=9;
    assert(!sst_animation_select(52,&normal,0,0,&selected)); // only carry offset exists
    step(&carry,11);step(&carry,12); // no observed geometry: active manifest empties
    assert(!sst_animation_select(52,&carry,0,0,&selected));
    sst_animation_reset();
}
static void test_environment(const char *name,const char *value) {
#ifdef _WIN32
    assert(!_putenv_s(name,value?value:""));
#else
    assert(!(value?setenv(name,value,1):unsetenv(name)));
#endif
}
static char *copy_environment(const char *name) {
    const char *value=getenv(name);if(!value)return NULL;
    size_t size=strlen(value)+1;char *copy=malloc(size);assert(copy);memcpy(copy,value,size);return copy;
}
static void discard_rigid_fixture(SstPose *local) {
    char path[4096];
#ifdef _WIN32
    char directory[4096];DWORD size=GetTempPathA(sizeof directory,directory);
    assert(size && size<sizeof directory);assert(GetTempFileNameA(directory,"sst",0,path));
#else
    snprintf(path,sizeof path,"/tmp/sst-carry-child-XXXXXX");int fd=mkstemp(path);assert(fd>=0);close(fd);
#endif
    char *debug=copy_environment("LARECOMP_DEBUG_DATA"),*drop=copy_environment("SST_TEST_RIGID_DROP_FILE");
    test_environment("SST_TEST_RIGID_DROP_FILE",path);test_environment("LARECOMP_DEBUG_DATA","1");
    step(local,21); // the existing public frame hook drops rigid samples only
    test_environment("LARECOMP_DEBUG_DATA",debug);test_environment("SST_TEST_RIGID_DROP_FILE",drop);
    free(debug);free(drop);remove(path);
}
static void verified_children_survive_rigid_loss_both_contexts(void) {
    begin();static float body[368*12];SstPose local={.anim_id=20,.action=3};
    for(unsigned context=0;context<2;context++) {
        local.equip_flags=context?SST_POSE_CONTEXT_SMALL_CARRY:0;
        for(unsigned phase=0;phase<=20;phase++) {
            for(unsigned j=0;j<368;j++)memcpy(body+j*12,identity,48);
            body[3*12+3]=10;body[3*12+7]=context?120:50;
            body[5*12+3]=20;body[5*12+7]=context?150:80;
            step(&local,(float)phase);
            sst_animation_capture(6,&local,identity,identity,body,sizeof body,NULL,0);
            for(unsigned mesh=52;mesh<=53;mesh++) {
                unsigned joint=mesh==52?3:5;float offset[12],hand[12];
                child_offset(local.equip_flags,mesh,offset);assert(sst_hand_compose(body+joint*12,offset,hand));
                sst_animation_capture((uint16_t)mesh,&local,identity,hand,NULL,0,NULL,0);
            }
        }
    }
    discard_rigid_fixture(&local);
    assert(!sst_animation_stats().attachments); // legacy exact-joint table was never widened
    for(unsigned context=0;context<2;context++) {
        SstPose remote={.anim_id=20,.action=3,.anim_frame=9,
            .equip_flags=context?SST_POSE_CONTEXT_SMALL_CARRY|0xe1:0xe1};
        for(unsigned mesh=52;mesh<=53;mesh++) {
            SstAnimationPose selected;assert(sst_animation_select((uint16_t)mesh,&remote,0,0,&selected));
            assert(selected.attachment && selected.child);
            SstHandOffset offset;assert(sst_hand_lookup((uint16_t)mesh,&remote,true,45,&offset));
            float wanted_local[12];child_offset(context?SST_POSE_CONTEXT_SMALL_CARRY:0,mesh,wanted_local);
            matrix_near(offset.local,wanted_local);
            float joint[12],expected[12],uniform[28]={0};memcpy(joint,identity,48);
            joint[3]=mesh==52?10:20;joint[7]=context?(mesh==52?120:150):(mesh==52?50:80);
            assert(sst_hand_compose(joint,wanted_local,expected));
            assert(sst_animation_uniform(&selected,&remote,identity,0,uniform,sizeof uniform));
            matrix_near(uniform+16,expected);
            if(context) {
                SstAnimationPose corrupt=selected;corrupt.child=false;
                assert(!sst_animation_uniform(&corrupt,&remote,identity,0,uniform,sizeof uniform));
                SstPose wrong=remote;wrong.equip_flags=0;
                assert(!sst_animation_uniform(&selected,&wrong,identity,0,uniform,sizeof uniform));
            }
        }
    }
    assert(!sst_hand_stats().blocked && !sst_hand_stats().blocked_parents && sst_hand_stats().entries<=512);
    SstPose unknown={.anim_id=20,.action=3,.anim_frame=9,.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED};
    SstAnimationPose selected;assert(!sst_animation_select(52,&unknown,0,0,&selected));
    sst_animation_reset();
}
static void context_budget(void) {
    begin();SstPose carry={.anim_id=20,.action=3,.equip_flags=SST_POSE_CONTEXT_SMALL_CARRY};
    for(unsigned i=0;i<=6;i++)step(&carry,(float)i);
    size_t bytes=65520;float *palette=calloc(1,bytes);assert(palette);
    for(unsigned i=0;i<180;i++) {
        step(&carry,(float)((6+i)%45));palette[3]=(float)i;
        assert(sst_animation_capture((uint16_t)(100+i),&carry,identity,identity,palette,bytes,palette,bytes));
        assert(sst_animation_stats().bytes<=SST_ANIMATION_BUDGET && sst_animation_stats().samples<=2048);
    }
    assert(sst_animation_stats().samples<180);free(palette);sst_animation_reset();
    assert(!sst_animation_stats().bytes && !sst_animation_stats().samples);
}
int main(void) {
    if(getenv("SST_TEST_RIGID_DROP_FILE") && getenv("LARECOMP_DEBUG_DATA")) {
        fputs("Unset SST_TEST_RIGID_DROP_FILE before this pure fixture.\n",stderr);return 2;
    }
    uint32_t tick=sst_game_pose_tick();
    assert_separated(20,3,false);assert_separated(20,3,true);
    assert_separated(26,67,false);assert_separated(26,67,true);
    context_settling();neutral_is_context_zero();normal_attachment_not_poisoned();
    normal_child_not_poisoned();context_budget();
    child_context_entries_and_parents();child_body_context_required();
    verified_children_survive_rigid_loss_both_contexts();
    assert(sst_game_pose_tick()==tick);
    assert(sst_avatar_pose_mesh(61,0,0)==52 && sst_avatar_pose_mesh(62,0,0)==53);
    assert(!sst_avatar_pose_mesh(61,17664,17664) && !sst_avatar_pose_mesh(62,0,48));
    puts("carry context tests passed: separate palettes, neutral0, verified child0/4, bounded cache; no cold pose generation claimed");
    return 0;
}

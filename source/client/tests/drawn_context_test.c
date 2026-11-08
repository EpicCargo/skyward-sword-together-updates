// Numeric public-API regressions only: no game assets, native actor or cold poses.
#include "../src/sst_animation.h"
#include "../src/sst_avatar.h"
#include "../src/sst_hand.h"
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
static void begin(void) {sst_animation_reset();frame=0;}
static void step(SstPose *p,float phase) {
    p->tick++;p->anim_frame=phase;
    sst_animation_frame(&scene,p,++frame);
    assert(sst_animation_set_length(p->anim_id,45));
}
static void assert_close(float a,float b) {assert(fabsf(a-b)<.03f);}
static void matrix_near(const float *a,const float *b) {for(unsigned i=0;i<12;i++)assert_close(a[i],b[i]);}
static void bounded(void) {
    SstAnimationStats a=sst_animation_stats();
    assert(a.bytes<=SST_ANIMATION_BUDGET && a.samples<=2048);
    assert(sst_hand_stats().entries<=512);
}
static bool capture_small(uint16_t mesh,const SstPose *p,float y) {
    float positions[12],normals[12];memcpy(positions,identity,48);memcpy(normals,identity,48);
    positions[7]=y;normals[0]=(p->equip_flags&SST_EQUIP_SWORD_DRAWN)?2:1;
    bool ok=sst_animation_capture(mesh,p,identity,identity,positions,48,normals,48);
    bounded();return ok;
}
static void learn_small(SstPose *p,float y,bool second,bool missing_second) {
    for(unsigned phase=0;phase<=12;phase++) {
        step(p,(float)phase);bool ok=capture_small(1,p,y);if(phase>=6)assert(ok);
        if(second){ok=capture_small(2,p,y+5);if(phase>=6)assert(ok);}
        if(missing_second) {
            SstPose unknown=*p;unknown.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED;
            assert(!capture_small(2,&unknown,y+5)); // still declares live skinned layout
        }
    }
}
static float small_y(uint16_t mesh,const SstPose *p,SstAnimationPose *selected) {
    SstAnimationPose s;assert(sst_animation_select(mesh,p,48,48,&s));
    float out[12];assert(sst_animation_uniform(&s,p,identity,3,out,48));
    if(selected)*selected=s;
    return out[7];
}
static void sample_separation(unsigned carry,bool drawn_first) {
    begin();SstPose normal={.anim_id=20,.action=3,.equip_flags=(uint8_t)carry},drawn=normal;
    drawn.equip_flags|=SST_EQUIP_SWORD_DRAWN;
    float a=10+(float)carry,b=80+(float)carry;
    if(drawn_first){learn_small(&drawn,b,false,false);learn_small(&normal,a,false,false);}
    else {learn_small(&normal,a,false,false);learn_small(&drawn,b,false,false);}
    normal.anim_frame=drawn.anim_frame=9;
    SstAnimationPose sa,sb;
    float observed_normal=small_y(1,&normal,&sa),observed_drawn=small_y(1,&drawn,&sb);
    printf("drawn identity carry=%u order=%s normal_y=%.1f drawn_y=%.1f expected=%.1f/%.1f\n",
           carry,drawn_first?"drawn-first":"normal-first",observed_normal,observed_drawn,a,b);
    fflush(stdout);assert_close(observed_normal,a);assert_close(observed_drawn,b);
    float out[12];assert(sst_animation_uniform(&sa,&normal,identity,4,out,48));assert_close(out[0],1);
    assert(sst_animation_uniform(&sb,&drawn,identity,4,out,48));assert_close(out[0],2);
    for(unsigned i=0;i<12;i++)out[i]=123;
    assert(!sst_animation_uniform(&sa,&drawn,identity,3,out,48));
    for(unsigned i=0;i<12;i++)assert_close(out[i],123);
    assert(!sst_animation_uniform(&sb,&normal,identity,3,out,48));
    SstAnimationPose mixed=sa;mixed.after=sb.after;
    assert(!sst_animation_uniform(&mixed,&normal,identity,3,out,48));
    normal.equip_flags|=0xe1;drawn.equip_flags|=0xe1; // ownership/world bits are irrelevant
    assert_close(small_y(1,&normal,NULL),a);assert_close(small_y(1,&drawn,NULL),b);
    // The local same-primary shortcut must not equate the opposite sword state.
    assert(!sst_animation_skip_rigid(&normal,&normal));
    assert(sst_animation_skip_rigid(&normal,&drawn));
    bounded();sst_animation_reset();
}
static void settling_and_unknown(void) {
    begin();SstPose p={.anim_id=20,.action=3};learn_small(&p,10,false,false);
    p.equip_flags=SST_EQUIP_SWORD_DRAWN;step(&p,13);assert(!capture_small(1,&p,80));
    for(unsigned i=0;i<8;i++){step(&p,13);assert(!capture_small(1,&p,80));}
    for(unsigned i=1;i<=6;i++) {
        p.equip_flags=SST_EQUIP_SWORD_DRAWN|(i%2?0x81:0x21);
        step(&p,13+(float)i);assert(capture_small(1,&p,80)==(i==6));
    }
    p.equip_flags=0;step(&p,20);assert(!capture_small(1,&p,10));
    for(unsigned i=1;i<=6;i++){step(&p,20+(float)i);assert(capture_small(1,&p,10)==(i==6));}
    for(unsigned drawn=0;drawn<2;drawn++) {
        p.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED|(drawn?SST_EQUIP_SWORD_DRAWN:0);
        for(unsigned i=0;i<9;i++){step(&p,27+(float)i);assert(!capture_small(1,&p,99));}
        SstAnimationPose selected;assert(!sst_animation_select(1,&p,48,48,&selected));
    }
    sst_animation_reset();
}
static void neutral_identity(void) {
    begin();SstPose idle={.anim_id=26,.action=0};learn_small(&idle,10,true,false);
    SstPose drawn={.anim_id=20,.action=3,.equip_flags=0xe3};
    learn_small(&drawn,80,false,true);step(&drawn,13);
    SstPose remote=drawn;remote.anim_frame=9;remote.pos[0]=100;remote.pos[1]=20;remote.pos[2]=-30;
    SstPose unchanged=remote;SstAnimationReplay replay;assert(sst_animation_replay_prepare(&remote,&replay));
    assert(replay.ready && replay.cohort && replay.pose.anim_id==26 && !replay.pose.action);
    assert(replay.pose.equip_flags==0xe1 && !(replay.pose.equip_flags&SST_POSE_IDENTITY_MASK));
    assert(!memcmp(&remote,&unchanged,sizeof remote)); // requested equipment remains drawn
    assert(!memcmp(replay.pose.pos,remote.pos,sizeof remote.pos));
    uint64_t cohort=replay.cohort;
    for(unsigned mesh=1;mesh<=2;mesh++) {
        SstAnimationPose selected;float out[12];
        assert(sst_animation_replay_select((uint16_t)mesh,&replay,48,48,&selected));
        assert(sst_animation_uniform(&selected,&replay.pose,identity,3,out,48));assert_close(out[7],30+5*(float)(mesh-1));
    }
    SstAnimationReplay corrupt=replay;corrupt.pose.equip_flags|=SST_EQUIP_SWORD_DRAWN;
    SstAnimationPose selected;assert(!sst_animation_replay_select(1,&corrupt,48,48,&selected));
    for(unsigned draw=0;draw<2;draw++) {
        remote.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED|0xe1|(draw?SST_EQUIP_SWORD_DRAWN:0);
        assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort==cohort && replay.pose.equip_flags==0xe1);
    }
    // No nonzero identity, even idle26/action0, may manufacture a neutral cohort.
    const uint8_t contexts[]={2,4,6};
    for(unsigned i=0;i<3;i++) {
        begin();idle=(SstPose){.anim_id=26,.equip_flags=contexts[i]};learn_small(&idle,80,true,false);step(&idle,13);
        assert(!sst_animation_replay_stats().neutral_parts);
        remote=(SstPose){.anim_id=168,.action=2};assert(!sst_animation_replay_prepare(&remote,&replay));
    }
    sst_animation_reset();
}
static void offset_for(unsigned context,unsigned mesh,float out[12]) {
    memcpy(out,identity,48);float angle=.1f*(float)context;
    out[0]=out[5]=cosf(angle);out[1]=-sinf(angle);out[4]=sinf(angle);
    out[3]=26.5f+(float)context+(mesh==53?1:0);out[7]=(float)context/2;
}
static void observe_child(uint16_t mesh,SstPose *p,const float *joint,const float *offset) {
    float model[12];assert(sst_hand_compose(joint,offset,model));
    sst_hand_observe(mesh,p,6,368*48,0,joint,model);
}
static void child_parent_banks(bool drawn_first) {
    begin();float joint[12];memcpy(joint,identity,48);joint[7]=50;
    const uint8_t contexts[]={0,2,4,6};SstPose pose[4];
    for(unsigned carry=0;carry<2;carry++)for(unsigned pass=0;pass<2;pass++) {
        unsigned index=carry*2+(drawn_first?1-pass:pass),context=contexts[index];
        pose[index]=(SstPose){.anim_id=20,.action=3,.anim_frame=9,.tick=100,.equip_flags=(uint8_t)context};
        float offset[12];offset_for(context,52,offset);observe_child(52,&pose[index],joint,offset);
        SstHandOffset got;assert(!sst_hand_lookup(52,&pose[index],true,45,&got));
        for(unsigned repeat=0;repeat<5;repeat++)observe_child(52,&pose[index],joint,offset);
        assert(!sst_hand_lookup(52,&pose[index],true,45,&got)); // distinct ticks remain mandatory
        pose[index].tick++;observe_child(52,&pose[index],joint,offset);
        assert(sst_hand_lookup(52,&pose[index],true,45,&got));matrix_near(got.local,offset);
        SstPose other=pose[index];other.equip_flags^=SST_EQUIP_SWORD_DRAWN;
        if(!pass)assert(!sst_hand_lookup(52,&other,true,45,&got));
    }
    assert(sst_hand_stats().ready==4 && !sst_hand_stats().blocked_parents);
    for(unsigned i=0;i<4;i++) {
        SstHandOffset got;float offset[12];offset_for(contexts[i],52,offset);pose[i].equip_flags|=0xe1;
        assert(sst_hand_lookup(52,&pose[i],true,45,&got));matrix_near(got.local,offset);
    }
    // An inconsistent drawn parent must not block normal or either carry identity.
    float bad[12];offset_for(2,52,bad);bad[3]+=1;
    for(unsigned i=0;i<3;i++){pose[1].tick++;observe_child(52,&pose[1],joint,bad);}
    SstHandOffset got;assert(!sst_hand_lookup(52,&pose[1],true,45,&got));
    assert(sst_hand_stats().blocked_parents==1);
    for(unsigned i=0;i<4;i++)if(i!=1)assert(sst_hand_lookup(52,&pose[i],true,45,&got));
    size_t entries=sst_hand_stats().entries;
    for(unsigned drawn=0;drawn<2;drawn++) {
        SstPose unknown=pose[0];unknown.equip_flags=SST_POSE_CONTEXT_UNSUPPORTED|(drawn?2:0);
        observe_child(52,&unknown,joint,bad);assert(!sst_hand_lookup(52,&unknown,true,45,&got));
    }
    assert(sst_hand_stats().entries==entries);sst_hand_forget_clip(20);assert(!sst_hand_stats().entries);
    sst_animation_reset();
}
static void environment(const char *name,const char *value) {
#ifdef _WIN32
    assert(!_putenv_s(name,value?value:""));
#else
    assert(!(value?setenv(name,value,1):unsetenv(name)));
#endif
}
static char *saved_environment(const char *name) {
    const char *value=getenv(name);if(!value)return NULL;
    size_t size=strlen(value)+1;char *copy=malloc(size);assert(copy);memcpy(copy,value,size);return copy;
}
static void drop_rigids(SstPose *p) {
    char path[4096];
#ifdef _WIN32
    char directory[4096];DWORD length=GetTempPathA(sizeof directory,directory);
    assert(length && length<sizeof directory);assert(GetTempFileNameA(directory,"sst",0,path));
#else
    snprintf(path,sizeof path,"/tmp/sst-drawn-child-XXXXXX");int fd=mkstemp(path);assert(fd>=0);close(fd);
#endif
    char *debug=saved_environment("LARECOMP_DEBUG_DATA"),*drop=saved_environment("SST_TEST_RIGID_DROP_FILE");
    environment("LARECOMP_DEBUG_DATA","1");environment("SST_TEST_RIGID_DROP_FILE",path);step(p,21);
    environment("LARECOMP_DEBUG_DATA",debug);environment("SST_TEST_RIGID_DROP_FILE",drop);
    free(debug);free(drop);remove(path);
}
static void child_automatic_and_donor_context(void) {
    begin();static float body[368*12];SstPose p={.anim_id=20,.action=3};
    for(unsigned context=0;context<=6;context+=2) {
        p.equip_flags=(uint8_t)context;
        for(unsigned phase=0;phase<=20;phase++) {
            for(unsigned j=0;j<368;j++)memcpy(body+j*12,identity,48);
            body[3*12+7]=50+10*(float)context;body[5*12+7]=80+10*(float)context;
            step(&p,(float)phase);sst_animation_capture(6,&p,identity,identity,body,sizeof body,NULL,0);
            for(unsigned mesh=52;mesh<=53;mesh++) {
                float offset[12],model[12];offset_for(context,mesh,offset);
                assert(sst_hand_compose(body+(mesh==52?3:5)*12,offset,model));
                sst_animation_capture((uint16_t)mesh,&p,identity,model,NULL,0,NULL,0);
            }
        }
    }
    drop_rigids(&p);assert(!sst_animation_stats().attachments); // none exactly equals a joint
    for(unsigned context=0;context<=6;context+=2)for(unsigned mesh=52;mesh<=53;mesh++) {
        SstPose remote={.anim_id=20,.action=3,.anim_frame=9,.equip_flags=(uint8_t)(context|0xe1)};
        SstAnimationPose selected;assert(sst_animation_select((uint16_t)mesh,&remote,0,0,&selected));
        assert(selected.attachment && selected.child);
        float joint[12],offset[12],expected[12],out[28]={0};memcpy(joint,identity,48);
        joint[7]=(mesh==52?50:80)+10*(float)context;offset_for(context,mesh,offset);
        assert(sst_hand_compose(joint,offset,expected));
        assert(sst_animation_uniform(&selected,&remote,identity,0,out,sizeof out));matrix_near(out+16,expected);
        SstPose other=remote;other.equip_flags^=2;
        assert(!sst_animation_uniform(&selected,&other,identity,0,out,sizeof out));
        if(context&4){SstAnimationPose corrupt=selected;corrupt.child=false;assert(!sst_animation_uniform(&corrupt,&remote,identity,0,out,sizeof out));}
    }
    assert(!sst_hand_stats().blocked && !sst_hand_stats().blocked_parents);bounded();
    // A verified child offset alone cannot borrow the opposite state's body.
    begin();p=(SstPose){.anim_id=20,.action=3};
    for(unsigned j=0;j<368;j++)memcpy(body+j*12,identity,48);
    for(unsigned phase=0;phase<=12;phase++){step(&p,(float)phase);sst_animation_capture(6,&p,identity,identity,body,sizeof body,NULL,0);}
    SstPose drawn=p;drawn.anim_frame=9;drawn.equip_flags=2;float offset[12];offset_for(2,52,offset);
    observe_child(52,&drawn,body+3*12,offset);drawn.tick++;observe_child(52,&drawn,body+3*12,offset);
    SstHandOffset got;assert(sst_hand_lookup(52,&drawn,true,45,&got));
    SstAnimationPose selected;assert(!sst_animation_select(52,&drawn,0,0,&selected));
    // Neither same-frame opposite-context body nor rigid draw may seed observations.
    drawn=p;drawn.equip_flags=2;
    assert(!sst_animation_capture(6,&drawn,identity,identity,body,sizeof body,NULL,0));
    assert(!sst_animation_capture(53,&drawn,identity,identity,NULL,0,NULL,0));
    step(&p,13);assert(!sst_hand_lookup(53,&drawn,true,45,&got));
    sst_animation_reset();
}
static void learn_legacy(uint8_t context,uint16_t clip,bool same_joints) {
    SstPose p={.anim_id=clip,.action=3,.equip_flags=context};float body[36];
    for(unsigned phase=0;phase<=36;phase++) {
        for(unsigned j=0;j<3;j++)memcpy(body+j*12,identity,48);
        body[12+7]=context?70:20;body[24+7]=context?80:30;
        step(&p,(float)phase);sst_animation_capture(6,&p,identity,identity,body,sizeof body,NULL,0);
        if(phase<=12)for(unsigned side=0;side<2;side++) {
            unsigned joint=side?2:1;if(context && !same_joints)joint=3-joint;
            sst_animation_capture(side?23:34,&p,identity,body+joint*12,NULL,0,NULL,0);
        }
    }
}
static void legacy_hero_banks(bool drawn_first) {
    begin();
    if(drawn_first){learn_legacy(2,20,false);learn_legacy(0,20,false);}
    else {learn_legacy(0,20,false);learn_legacy(2,20,false);}
    for(unsigned context=0;context<=2;context+=2)for(unsigned side=0;side<2;side++) {
        SstPose remote={.anim_id=20,.action=3,.anim_frame=30,.equip_flags=(uint8_t)(context|0xe1)};
        SstAnimationPose selected;assert(sst_animation_select(side?23:34,&remote,0,0,&selected));
        assert(selected.attachment && !selected.child);unsigned joint=side?2:1;if(context)joint=3-joint;
        assert(selected.joint==joint);float out[28]={0};
        assert(sst_animation_uniform(&selected,&remote,identity,0,out,sizeof out));
        assert_close(out[16+7],context?(joint==1?70:80):(joint==1?20:30));
        SstPose wrong=remote;wrong.equip_flags^=2;assert(!sst_animation_uniform(&selected,&wrong,identity,0,out,sizeof out));
        remote.equip_flags|=4;assert(!sst_animation_select(side?23:34,&remote,0,0,&selected)); // carry has no legacy donor
    }
    assert(sst_animation_stats().attachments>=4 && !sst_hand_stats().entries);
    assert(!sst_animation_wrist_link(34,NULL,NULL,NULL)); // contradictory per-context joint is not diagnostic proof
    assert(sst_animation_set_length(20,44));
    SstPose remote={.anim_id=20,.action=3,.anim_frame=30};SstAnimationPose selected;
    assert(!sst_animation_select(34,&remote,0,0,&selected));remote.equip_flags=2;assert(!sst_animation_select(34,&remote,0,0,&selected));
    assert(!sst_animation_stats().attachments);sst_animation_reset();
}
static void conservative_wrist_diagnostic(void) {
    begin();learn_legacy(0,20,true);learn_legacy(2,20,true);
    assert(!sst_animation_wrist_link(34,NULL,NULL,NULL)); // two banks of ONE clip are not two distinct clips
    learn_legacy(0,21,true);assert(sst_animation_wrist_link(34,NULL,NULL,NULL));
    assert(sst_animation_set_length(20,44));assert(!sst_animation_wrist_link(34,NULL,NULL,NULL));
    sst_animation_reset();
}
static void identity_budget(void) {
    begin();size_t size=65520;float *palette=calloc(1,size);assert(palette);
    for(unsigned context=0;context<=6;context+=2) {
        SstPose p={.anim_id=20,.action=3,.equip_flags=(uint8_t)context};
        for(unsigned i=0;i<=6;i++)step(&p,(float)i);
        for(unsigned i=0;i<45;i++) {
            step(&p,(float)((6+i)%45));palette[3]=(float)(context*100+i);
            assert(sst_animation_capture((uint16_t)(100+context*45+i),&p,identity,identity,palette,size,palette,size));bounded();
        }
    }
    assert(sst_animation_stats().samples<180);free(palette);sst_animation_reset();
    assert(!sst_animation_stats().bytes && !sst_animation_stats().samples && !sst_hand_stats().entries);
}
int main(void) {
    if(getenv("SST_TEST_RIGID_DROP_FILE") && getenv("LARECOMP_DEBUG_DATA")) {
        fputs("Unset SST_TEST_RIGID_DROP_FILE before this numeric fixture.\n",stderr);return 2;
    }
    sample_separation(0,false);sample_separation(0,true);
    sample_separation(4,false);sample_separation(4,true);
    settling_and_unknown();neutral_identity();child_parent_banks(false);child_parent_banks(true);
    child_automatic_and_donor_context();legacy_hero_banks(false);legacy_hero_banks(true);
    conservative_wrist_diagnostic();identity_budget();
    puts("drawn context: separate0/2/4/6 palettes, child parents, normal/drawn hero legacy, coherent neutral and bounded storage; NOT native/cold-pose proof");
    return 0;
}

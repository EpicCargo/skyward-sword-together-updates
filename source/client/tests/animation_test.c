#include "../src/sst_animation.h"
#include "../src/sst_pose_buffer.h"
#include "../src/sst_avatar.h"
#include "../src/sst_hand.h"
#include <assert.h>
#include <math.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static void test_environment(const char *name,const char *value) {
#ifdef _WIN32
    assert(!_putenv_s(name,value?value:""));
#else
    assert(!(value?setenv(name,value,1):unsetenv(name)));
#endif
}

static const float identity[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
static uint64_t frame;
static void settle(const SstScene *scene, const SstPose *p) {
    SstPose moving=*p;
    for (int i = 0; i < 14; i++) {
        moving.anim_frame=p->anim_frame+i*.5f;
        sst_animation_frame(scene,&moving,++frame);
    }
    sst_animation_frame(scene,p,++frame); // Wrap back to the requested phase.
    assert(sst_animation_set_length(p->anim_id,p->anim_id==26 ? 45 : 24));
}
static void closef(float a, float b) { assert(fabsf(a-b) < .003f); }
static void equipment_cohorts(void) {
    SstScene scene={.stage="F001r",.room=0,.layer=1};
    SstPose local={.anim_id=26,.action=0},unknown={.anim_id=168,.action=2,.pos={100,0,0}};
    float body[12],hand[12],equipment[12];
    memcpy(body,identity,48);memcpy(hand,identity,48);memcpy(equipment,identity,48);
    body[7]=10;hand[7]=20;equipment[7]=30;
    sst_animation_reset();
    for(unsigned i=0;i<10;i++) {
        local.anim_frame=i;local.tick++;
        sst_animation_frame(&scene,&local,++frame);sst_animation_set_length(26,45);
        sst_animation_capture(1,&local,identity,identity,body,48,NULL,0);
        sst_animation_capture(sst_avatar_pose_mesh(14,0,0),&local,identity,hand,NULL,0,NULL,0);
        sst_animation_capture(55,&local,identity,equipment,NULL,0,NULL,0);
    }
    SstAnimationReplay neutral;SstAnimationPose pose;
    assert(sst_animation_replay_prepare(&unknown,&neutral) && neutral.cohort);
    uint64_t cohort=neutral.cohort;
    assert(sst_animation_replay_select(55,&neutral,0,0,&pose));
    assert(!sst_animation_replay_select(58,&neutral,0,0,&pose)); // opposite sword state is never borrowed
    local.anim_id=19;local.equip_flags=SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN;
    for(unsigned i=0;i<10;i++) {
        local.anim_frame=i;local.tick++;
        sst_animation_frame(&scene,&local,++frame);sst_animation_set_length(19,45);
        sst_animation_capture(1,&local,identity,identity,body,48,NULL,0);
        sst_animation_capture(sst_avatar_pose_mesh(54,0,0),&local,identity,hand,NULL,0,NULL,0);
        sst_animation_capture(58,&local,identity,equipment,NULL,0,NULL,0);
    }
    assert(sst_animation_replay_prepare(&unknown,&neutral) && neutral.cohort==cohort);
    assert(sst_animation_replay_select(1,&neutral,48,0,&pose));
    assert(sst_animation_replay_select(53,&neutral,0,0,&pose)); // observer's drawn hand shares the verified idle node
    assert(sst_animation_replay_select(55,&neutral,0,0,&pose));
    assert(!sst_animation_replay_select(58,&neutral,0,0,&pose));
    SstPose drawn=unknown;drawn.anim_id=19;drawn.action=0;drawn.anim_frame=6;
    drawn.equip_flags=SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN;
    assert(sst_animation_replay_prepare(&drawn,&neutral) && !neutral.cohort);
    assert(sst_animation_replay_select(58,&neutral,0,0,&pose));
    assert(!sst_animation_replay_select(55,&neutral,0,0,&pose));
    drawn.action=2;assert(sst_animation_replay_prepare(&drawn,&neutral) && neutral.cohort==cohort);
    assert(!(neutral.pose.equip_flags&SST_POSE_IDENTITY_MASK));
    assert(neutral.pose.equip_flags==SST_EQUIP_SWORD && drawn.equip_flags==(SST_EQUIP_SWORD|SST_EQUIP_SWORD_DRAWN));
    assert(sst_animation_stats().bytes<=SST_ANIMATION_BUDGET);
    sst_animation_reset();assert(!sst_animation_replay_prepare(&drawn,&neutral));
}
static void xyz(const float m[12], float x, float y, float z) {
    closef(m[3], x); closef(m[7], y); closef(m[11], z);
}
static void independent_replay(void) {
    SstScene scene={.stage="F001r",.room=1,.layer=1};
    SstPose viewer={.anim_id=26,.action=0,.tick=500};
    float body[12],rigid[12],normal[12],uniform[216]={0},result[12];
    memcpy(body,identity,48);memcpy(rigid,identity,48);memcpy(normal,identity,48);
    SstAnimationReplay replay;SstAnimationPose pose;
    SstPose remote={.anim_id=168,.action=2,.anim_frame=20,.pos={100,0,0}};
    // An unsupported idle variation at a completely cold cache has no verified
    // body. The caller must suppress it, never reuse a viewer sword/step pose.
    sst_animation_reset();viewer.anim_id=28;
    sst_animation_frame(&scene,&viewer,++frame);
    assert(!sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48));
    sst_animation_frame(&scene,&viewer,++frame);
    assert(!sst_animation_replay_prepare(&remote,&replay) && !replay.ready);
    assert(!sst_animation_replay_select(1,&replay,48,48,&pose));
    assert(sst_animation_replay_stats().unavailable_frames==1);
    assert(sst_animation_replay_stats().suppressed_parts==1);

    sst_animation_reset();viewer.anim_id=26;
    // Body and rigid transforms visibly change each source frame. The neutral
    // snapshot must keep both from ONE fully settled frame (native phase six).
    for(unsigned i=0;i<10;i++) {
        viewer.anim_frame=i;viewer.tick++;sst_animation_frame(&scene,&viewer,++frame);
        assert(sst_animation_set_length(26,45));body[7]=i+10;rigid[7]=i+20;
        sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48);
        sst_animation_capture(10,&viewer,identity,rigid,NULL,0,NULL,0);
    }
    assert(sst_animation_replay_stats().neutral_parts==2);
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort && replay.pose.anim_id==26);
    assert(replay.pose.action==0 && replay.pose.anim_frame==6 && replay.pose.pos[0]==100);
    assert(sst_animation_replay_select(1,&replay,48,48,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,3,result,48));xyz(result,100,16,0);
    assert(!sst_animation_uniform(&pose,&remote,identity,3,result,48)); // clip/action gates unchanged
    assert(sst_animation_replay_select(10,&replay,0,0,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,0,uniform,sizeof uniform));xyz(uniform+16,100,26,0);
    assert(!sst_animation_replay_select(1,&replay,96,48,&pose));
    assert(!sst_animation_replay_select(11,&replay,0,0,&pose)); // no guessed wrist/rigid association

    // Viewer swings, remote remains in the native idle-stretch variation. Even
    // after learning the viewer's swing, the remote stays in the same idle cohort.
    viewer.anim_id=197;viewer.action=109;viewer.anim_frame=0;
    sst_animation_frame(&scene,&viewer,++frame);assert(sst_animation_set_length(197,12));
    viewer.anim_frame=1.5f;viewer.tick++;sst_animation_frame(&scene,&viewer,++frame);
    body[7]=99;rigid[7]=199;
    assert(sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48));
    assert(sst_animation_capture(10,&viewer,identity,rigid,NULL,0,NULL,0));
    viewer.tick++;sst_animation_frame(&scene,&viewer,++frame);
    sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48);
    sst_animation_capture(10,&viewer,identity,rigid,NULL,0,NULL,0);
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort);
    assert(sst_animation_replay_select(1,&replay,48,48,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,3,result,48));xyz(result,100,16,0);
    assert(sst_animation_replay_select(10,&replay,0,0,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,0,uniform,sizeof uniform));xyz(uniform+16,100,26,0);

    // Reverse: viewer idle, remote's cached sword clip follows its own pose.
    SstPose attack=remote;attack.anim_id=197;attack.action=109;attack.anim_frame=1.5f;
    viewer.anim_id=26;viewer.action=0;viewer.anim_frame=12;
    sst_animation_frame(&scene,&viewer,++frame);
    sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48);
    assert(sst_animation_replay_prepare(&attack,&replay) && !replay.cohort);
    assert(sst_animation_replay_select(1,&replay,48,48,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,3,result,48));xyz(result,100,99,0);

    // Unlearned distinct attacks must never borrow the other sword clip. Once
    // learned, each remains separate even while the viewer plays the other.
    attack.anim_id=198;
    assert(sst_animation_replay_prepare(&attack,&replay) && replay.cohort);
    viewer.anim_id=198;viewer.action=109;viewer.anim_frame=0;
    sst_animation_frame(&scene,&viewer,++frame);assert(sst_animation_set_length(198,12));
    viewer.anim_frame=1.5f;viewer.tick++;sst_animation_frame(&scene,&viewer,++frame);body[7]=77;
    assert(sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48));
    viewer.anim_id=197;viewer.anim_frame=1.5f;sst_animation_frame(&scene,&viewer,++frame);
    assert(sst_animation_replay_prepare(&attack,&replay) && !replay.cohort);
    assert(sst_animation_replay_select(1,&replay,48,48,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,3,result,48));xyz(result,100,77,0);

    // Unsupported viewer lock-on/sidestep cannot replace a remote idle pose.
    // A newly appearing rigid piece is hidden while the verified body stays visible.
    viewer.anim_id=28;viewer.action=0;viewer.anim_frame=3;sst_animation_frame(&scene,&viewer,++frame);
    body[7]=500;
    assert(!sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48));
    assert(!sst_animation_capture(11,&viewer,identity,rigid,NULL,0,NULL,0));
    sst_animation_frame(&scene,&viewer,++frame);
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort);
    assert(sst_animation_replay_select(1,&replay,48,48,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,3,result,48));xyz(result,100,16,0);
    assert(!sst_animation_replay_select(11,&replay,0,0,&pose));
    SstPose idle=remote;idle.anim_id=26;idle.action=0;idle.anim_frame=7;
    assert(sst_animation_replay_prepare(&idle,&replay) && !replay.cohort);
    assert(sst_animation_replay_select(1,&replay,48,48,&pose));
    assert(sst_animation_uniform(&pose,&replay.pose,identity,3,result,48));xyz(result,100,17,0);

    // Retained snapshots pause across a loader gap and resume only after live
    // geometry/layout evidence returns. An incompatible skinned layout fails closed.
    sst_animation_frame(NULL,NULL,++frame);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    scene.room++;sst_animation_frame(&scene,&viewer,++frame);
    sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48);
    sst_animation_frame(&scene,&viewer,++frame);
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort);
    float incompatible[24];memcpy(incompatible,body,48);memcpy(incompatible+12,body,48);
    sst_animation_capture(1,&viewer,identity,identity,incompatible,sizeof incompatible,normal,48);
    sst_animation_frame(&scene,&viewer,++frame);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    assert(sst_animation_stats().bytes<=SST_ANIMATION_BUDGET && sst_animation_stats().samples<=2048);
    assert(sst_animation_replay_stats().exact_frames>=3 && sst_animation_replay_stats().neutral_frames>=5);
    sst_animation_reset();
    assert(!sst_animation_replay_stats().neutral_parts && !sst_animation_replay_stats().neutral_frames);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    // A source tick changing between the body and rigid draws cannot publish a
    // neutral cohort. The following consistent frame can recover automatically.
    viewer=(SstPose){.anim_id=26,.action=0,.tick=900};
    for(unsigned i=0;i<=6;i++) {
        viewer.anim_frame=i;viewer.tick++;sst_animation_frame(&scene,&viewer,++frame);
        sst_animation_set_length(26,45);
        sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48);
        if(i==6)viewer.tick++;
        sst_animation_capture(10,&viewer,identity,rigid,NULL,0,NULL,0);
    }
    viewer.anim_frame=7;sst_animation_frame(&scene,&viewer,++frame);
    assert(!sst_animation_replay_stats().neutral_parts);
    assert(!sst_animation_replay_prepare(&remote,&replay));
    assert(sst_animation_capture(1,&viewer,identity,identity,body,48,normal,48));
    assert(sst_animation_capture(10,&viewer,identity,rigid,NULL,0,NULL,0));
    viewer.anim_frame=8;sst_animation_frame(&scene,&viewer,++frame);
    assert(sst_animation_replay_prepare(&remote,&replay) && replay.cohort && replay.pose.anim_frame==7);
    sst_animation_reset();
}
static void child_hands_survive_rigid_sample_loss(void) {
    static float body[368*12],normal[368*12];
    SstScene scene={.stage="F001r",.room=0,.layer=1};
    SstPose local={.anim_id=20,.action=3};
    sst_animation_reset();
    // A capture rejected for disagreeing with frame() cannot seed offsets.
    for(unsigned j=0;j<368;j++) {memcpy(body+j*12,identity,48);memcpy(normal+j*12,identity,48);}
    SstPose idle={.anim_id=26,.action=0};settle(&scene,&idle);
    for(unsigned i=0;i<4;i++) {
        idle.tick=500+i;idle.anim_frame=12+i;sst_animation_frame(&scene,&idle,++frame);
        SstPose rejected=idle;rejected.anim_id=20;rejected.action=3;rejected.anim_frame=6+i;
        float hand[12];memcpy(hand,identity,48);hand[3]=26.5f;
        assert(!sst_animation_capture(6,&rejected,identity,identity,body,sizeof body,normal,sizeof normal));
        assert(!sst_animation_capture(52,&rejected,identity,hand,NULL,0,NULL,0));
    }
    idle.tick++;sst_animation_frame(&scene,&idle,++frame);assert(!sst_hand_stats().entries);
    sst_animation_reset();
    for(unsigned i=0;i<35;i++) {
        local.tick=i+1;local.anim_frame=(float)(i%24);
        for(unsigned j=0;j<368;j++) {memcpy(body+j*12,identity,48);memcpy(normal+j*12,identity,48);}
        body[3*12+3]=10;body[3*12+7]=50+local.anim_frame;
        body[5*12+3]=20;body[5*12+7]=80+local.anim_frame;
        sst_animation_frame(&scene,&local,++frame);sst_animation_set_length(20,24);
        sst_animation_capture(6,&local,identity,identity,body,sizeof body,normal,sizeof normal);
        float offset[12];memcpy(offset,identity,48);offset[3]=26.5f;
        for(unsigned mesh=52;mesh<=53;mesh++) {
            float hand[12];unsigned joint=mesh==52?3:5;
            assert(sst_hand_compose(body+joint*12,offset,hand));
            sst_animation_capture(mesh,&local,identity,hand,NULL,0,NULL,0);
        }
    }
    assert(sst_hand_stats().ready && !sst_animation_stats().attachments);
    char path[4096];
#ifdef _WIN32
    char directory[4096];DWORD size=GetTempPathA(sizeof directory,directory);
    assert(size && size<sizeof directory);assert(GetTempFileNameA(directory,"sst",0,path));
#else
    snprintf(path,sizeof path,"/tmp/sst-child-XXXXXX");int fd=mkstemp(path);assert(fd>=0);close(fd);
#endif
    test_environment("LARECOMP_DEBUG_DATA","1");test_environment("SST_TEST_RIGID_DROP_FILE",path);
    local.tick++;sst_animation_frame(&scene,&local,++frame);
    test_environment("LARECOMP_DEBUG_DATA",NULL);test_environment("SST_TEST_RIGID_DROP_FILE",NULL);
    SstPose remote={.anim_id=20,.action=3,.anim_frame=9,.pos={100,0,0}};
    SstAnimationPose replay;float uniform[28]={0};
    for(unsigned mesh=52;mesh<=53;mesh++) {
        assert(sst_animation_select(mesh,&remote,0,0,&replay) && replay.child && replay.attachment);
        assert(sst_animation_uniform(&replay,&remote,identity,0,uniform,sizeof uniform));
        xyz(uniform+16,100+(mesh==52?10:20)+26.5f,(mesh==52?50:80)+9,0);
        float unchanged[28];memcpy(unchanged,uniform,sizeof uniform);
        SstAnimationPose corrupt=replay;corrupt.local[3]=NAN;
        assert(!sst_animation_uniform(&corrupt,&remote,identity,0,uniform,sizeof uniform));
        assert(!memcmp(unchanged,uniform,sizeof uniform));
        SstPose wrong=remote;wrong.action=67;
        assert(!sst_animation_select(mesh,&wrong,0,0,&replay));
    }
    assert(sst_hand_stats().placements==2);
    // A cached donor from an obsolete local layout must not attach to the new body.
    SstPose other=local;other.anim_id=168;other.action=2;other.tick++;
    sst_animation_frame(&scene,&other,++frame);
    assert(!sst_animation_capture(7,&other,identity,identity,body,sizeof body,normal,sizeof normal));
    other.tick++;sst_animation_frame(&scene,&other,++frame);
    assert(!sst_animation_select(52,&remote,0,0,&replay));
    sst_animation_set_length(20,25);assert(!sst_animation_select(52,&remote,0,0,&replay));
    sst_animation_reset();
}
int main(void) {
    SstScene scene = {.stage = "F001r", .room = 1, .layer = 1};
    SstPose idle = {.pos = {250,10,-400}, .anim_id = 26, .anim_frame = 6};
    SstPose walk = idle; walk.anim_id = 27; walk.anim_frame = 12;
    const float capture_view[12] = {0,0,1,100, 0,1,0,-80, -1,0,0,-300};
    // Capture a shoulder one unit along +X and ten above local Link's origin.
    float model[12] = {0,0,1,-300, 0,1,0,-70, -1,0,0,-550};
    float bone[12]; memcpy(bone, model, 48); bone[7] += 10; bone[11] -= 1;
    float normal[12]; memcpy(normal, capture_view, 48); normal[3]=normal[7]=normal[11]=0;
    sst_animation_reset();
    assert(!sst_animation_capture(1,&idle,capture_view,model,bone,48,normal,48));
    settle(&scene,&idle);
    assert(sst_animation_capture(1,&idle,capture_view,model,bone,48,normal,48));
    SstAnimationStats a=sst_animation_stats();
    assert(a.samples==1 && a.bytes>=144 && a.bytes<=400);
    // A repeated draw/frame bucket is reused, not reallocated.
    assert(sst_animation_capture(1,&idle,capture_view,model,bone,48,normal,48));
    assert(sst_animation_stats().captures==1);
    assert(sst_animation_capture(2,&idle,capture_view,model,bone,48,normal,48));
    assert(sst_animation_stats().samples==2 && sst_animation_stats().bytes==a.bytes);
    bone[7] += 2; // A buffer rewritten at the same address/frame must get a distinct snapshot.
    assert(sst_animation_capture(3,&idle,capture_view,model,bone,48,normal,48));
    assert(sst_animation_stats().bytes>a.bytes);
    bone[7] -= 2;
    // Learn a visibly different walk pose, after its native transition has settled.
    sst_animation_frame(&scene,&walk,++frame);
    assert(!sst_animation_capture(1,&walk,capture_view,model,bone,48,normal,48));
    settle(&scene,&walk);
    bone[7] += 8;
    assert(sst_animation_capture(1,&walk,capture_view,model,bone,48,normal,48));
    walk.anim_frame=15;
    sst_animation_frame(&scene,&walk,++frame);
    bone[7] += 6;
    assert(sst_animation_capture(1,&walk,capture_view,model,bone,48,normal,48));

    // Local is walking, remote is idle, and both the camera and remote yaw have changed.
    SstPose peer=idle; peer.pos[0]=500; peer.pos[1]=20; peer.pos[2]=-250; peer.rot[1]=16384;
    const float replay_view[12]={1,0,0,30, 0,1,0,40, 0,0,1,-70};
    SstAnimationPose s;
    assert(sst_animation_select(1,&peer,48,48,&s));
    float uniform[216];
    for (unsigned i=0;i<216;i++) uniform[i]=123;
    assert(sst_animation_uniform(&s,&peer,replay_view,0,uniform,sizeof uniform));
    xyz(uniform+16,530,60,-320);
    assert(uniform[0]==123 && uniform[28]==123); // Projection/material values are preserved.
    float result[12];
    assert(sst_animation_uniform(&s,&peer,replay_view,3,result,48));
    xyz(result,530,70,-321);
    assert(sst_animation_uniform(&s,&peer,replay_view,4,result,48));
    xyz(result,0,0,0);
    closef(result[2],1); closef(result[8],-1);
    peer.anim_id=27; peer.anim_frame=12;
    assert(sst_animation_select(1,&peer,48,48,&s));
    assert(sst_animation_uniform(&s,&peer,replay_view,3,result,48));
    xyz(result,530,78,-321);
    peer.anim_frame=13.5f;
    assert(sst_animation_select(1,&peer,48,48,&s));
    closef(s.blend,.5f);
    assert(sst_animation_uniform(&s,&peer,replay_view,3,result,48));
    xyz(result,530,81,-321);
    assert(!sst_animation_uniform(&s,&peer,replay_view,5,result,48));
    assert(!sst_animation_uniform(&s,&peer,replay_view,3,result,49));
    assert(!sst_animation_uniform(&s,&peer,replay_view,0,result,48));
    assert(!sst_animation_select(4,&peer,48,48,&s));
    assert(!sst_animation_select(1,&peer,96,48,&s));
    peer.anim_frame=100;
    assert(!sst_animation_select(1,&peer,48,48,&s));
    peer.anim_frame=NAN;
    assert(!sst_animation_select(1,&peer,48,48,&s));
    peer=idle; peer.anim_id=0; // Unsupported attack, rather than an uninitialized idle.
    assert(!sst_animation_select(1,&peer,48,48,&s));
    float bad_view[12]; memcpy(bad_view,identity,48); bad_view[0]=2;
    assert(!sst_animation_capture(1,&walk,bad_view,model,bone,48,normal,48));
    bad_view[0]=NAN;
    assert(!sst_animation_capture(1,&walk,bad_view,model,bone,48,normal,48));
    assert(!sst_animation_capture(1,&walk,identity,model,NULL,48,normal,48));
    assert(!sst_animation_capture(1,&walk,identity,model,bone,47,normal,48));

    // Canonical poses survive layer/room/stage changes, with a fresh capture settling period.
    // Geometry and palette layout still have to match; no source allocation is retained.
    size_t learned = sst_animation_stats().samples;
    size_t learned_bytes = sst_animation_stats().bytes;
    scene.layer++;
    sst_animation_frame(&scene,&idle,++frame);
    assert(sst_animation_stats().samples==learned && sst_animation_stats().resumes==1);
    assert(!sst_animation_capture(5,&idle,capture_view,model,bone,48,normal,48));
    peer=walk;peer.anim_frame=13.5f;peer.pos[0]=500;peer.pos[1]=20;peer.pos[2]=-250;peer.rot[1]=16384;
    assert(sst_animation_select(1,&peer,48,48,&s));
    assert(sst_animation_uniform(&s,&peer,replay_view,3,result,48));
    xyz(result,530,81,-321);
    assert(!sst_animation_select(4,&peer,48,48,&s));
    assert(!sst_animation_select(1,&peer,96,48,&s));
    scene.room++;
    sst_animation_frame(&scene,&idle,++frame);
    snprintf(scene.stage,sizeof scene.stage,"D000");
    sst_animation_frame(&scene,&idle,++frame);
    assert(sst_animation_stats().resumes==3 && sst_animation_stats().bytes==learned_bytes);
    // Loading/instance gaps suspend replay until a verified local draw resumes it.
    sst_animation_frame(NULL,NULL,++frame);
    assert(sst_animation_stats().bytes==learned_bytes);
    assert(!sst_animation_select(1,&peer,48,48,&s));
    sst_animation_frame(&scene,&idle,++frame);
    assert(sst_animation_stats().resumes==4 && sst_animation_select(1,&peer,48,48,&s));
    assert(sst_animation_uniform(&s,&peer,replay_view,3,result,48));
    xyz(result,530,81,-321);
    settle(&scene,&idle);
    assert(sst_animation_capture(1,&idle,identity,identity,NULL,0,NULL,0));
    sst_animation_frame(NULL,NULL,++frame);
    assert(sst_animation_stats().bytes>0);
    settle(&scene,&idle);
    assert(sst_animation_capture(1,&idle,identity,identity,NULL,0,NULL,0));
    sst_animation_frame(&scene,&idle,0);
    assert(sst_animation_stats().samples==0);

    // Large, changing palettes stay within the RAM budget and evict old samples.
    settle(&scene,&idle);
    size_t pn=65520;
    float *palette=calloc(1,pn); assert(palette);
    for (unsigned i=1;i<=180;i++) {
        sst_animation_frame(&scene,&idle,++frame); // Distinct captures, not one shared draw palette.
        assert(sst_animation_capture(i,&idle,identity,identity,palette,pn,palette,pn));
        assert(sst_animation_stats().bytes<=SST_ANIMATION_BUDGET);
    }
    assert(sst_animation_stats().samples<180);
    assert(!sst_animation_select(1,&idle,pn,pn,&s));
    assert(sst_animation_select(180,&idle,pn,pn,&s));
    free(palette);
    sst_animation_reset();
    assert(sst_animation_stats().samples==0 && sst_animation_stats().bytes==0);
    // Metadata also has a fixed cap, even for tiny rigid meshes.
    settle(&scene,&idle);
    for (unsigned i=1;i<=2200;i++)
        assert(sst_animation_capture(i,&idle,identity,identity,NULL,0,NULL,0));
    assert(sst_animation_stats().samples==2048);
    assert(!sst_animation_select(1,&idle,0,0,&s));
    sst_animation_reset();

    // Looping from the last frame to the first uses neighboring poses across the boundary.
    SstPose looping={.anim_id=26,.anim_frame=43};
    settle(&scene,&looping);
    float start_bone[12]; memcpy(start_bone,identity,48); start_bone[7]=10;
    assert(sst_animation_capture(1,&looping,identity,identity,start_bone,48,NULL,0));
    looping.anim_frame=1;
    sst_animation_frame(&scene,&looping,++frame);
    start_bone[7]=20;
    assert(sst_animation_capture(1,&looping,identity,identity,start_bone,48,NULL,0));
    looping.anim_frame=44.5f;
    assert(sst_animation_select(1,&looping,48,0,&s));
    closef(s.blend,.5f);
    assert(sst_animation_uniform(&s,&looping,identity,3,result,48));
    xyz(result,0,15,0);
    looping.anim_frame=46;
    assert(!sst_animation_select(1,&looping,48,0,&s));
    assert(sst_animation_set_length(26,60));
    assert(sst_animation_stats().samples==0);
    assert(!sst_animation_set_length(26,NAN));
    assert(!sst_animation_set_length(443,45));
    assert(!sst_animation_set_length(26,100000));
    sst_animation_reset();

    // At 120 Hz the same six native animation frames take 24 presentation frames to settle.
    looping.anim_frame=0;
    for (unsigned i=0;i<24;i++) {
        looping.anim_frame=i*.25f;
        sst_animation_frame(&scene,&looping,++frame);
    }
    assert(!sst_animation_capture(1,&looping,identity,identity,NULL,0,NULL,0));
    looping.anim_frame=6;
    sst_animation_frame(&scene,&looping,++frame);
    assert(sst_animation_capture(1,&looping,identity,identity,NULL,0,NULL,0));
    sst_animation_frame(NULL,NULL,++frame);
    assert(sst_animation_stats().bytes>0);
    sst_animation_reset();
    assert(sst_animation_stats().bytes==0 && !sst_animation_clip(26,NULL,NULL));

    // Native sword attacks are short, finite clips. Learn their middle without waiting six frames,
    // keep separate actions, and never borrow the final swing as the beginning of another attack.
    SstPose attack={.anim_id=197,.action=109,.anim_frame=8};
    assert(!sst_animation_clip(197,NULL,NULL));
    sst_animation_frame(&scene,&attack,++frame);
    assert(sst_animation_set_length(197,12));
    float length; bool loop;
    assert(sst_animation_clip(197,&length,&loop) && length==12 && !loop);
    for (unsigned i=1;i<=6;i++) {
        attack.anim_frame=8+i*.25f;
        sst_animation_frame(&scene,&attack,++frame);
        if (i<6) assert(!sst_animation_capture(1,&attack,identity,identity,start_bone,48,NULL,0));
    }
    assert(sst_animation_capture(1,&attack,identity,identity,start_bone,48,NULL,0));
    assert(sst_animation_select(1,&attack,48,0,&s));
    assert(sst_animation_stats().sword_captures==1 && sst_animation_stats().sword_hits==1);
    attack.anim_frame=0;
    assert(!sst_animation_select(1,&attack,48,0,&s));
    sst_animation_frame(&scene,&attack,++frame);
    assert(!sst_animation_capture(2,&attack,identity,identity,start_bone,48,NULL,0));
    attack.anim_frame=1.5f;
    sst_animation_frame(&scene,&attack,++frame);
    assert(sst_animation_capture(2,&attack,identity,identity,start_bone,48,NULL,0));
    attack.action=0;
    assert(!sst_animation_select(2,&attack,48,0,&s));
    assert(!sst_animation_capture(2,&attack,identity,identity,start_bone,48,NULL,0));
    attack.action=109; attack.anim_frame=12.1f;
    assert(!sst_animation_select(2,&attack,48,0,&s));
    assert(sst_animation_set_length(26,45));
    assert(sst_animation_clip(26,&length,&loop) && length==45 && loop);
    assert(!sst_animation_clip(424,NULL,NULL)); // Directional upper-body aiming is not a full pose.
    sst_animation_reset();
    assert(!sst_animation_clip(26,NULL,NULL));
    // Packed canonical palettes preserve small rotations and origin-relative translations.
    // Native camera/world transforms are applied after decoding, at full float precision.
    SstPose precision={.anim_id=26,.anim_frame=6};
    settle(&scene,&precision);
    const float values[12]={1.0003f,-.33333f,1e-7f,123.456f, -0.0f,1,0,65504, 0,0,1,-.000012f};
    assert(sst_animation_capture(1,&precision,identity,values,NULL,0,NULL,0));
    assert(sst_animation_select(1,&precision,0,0,&s));
    assert(sst_animation_uniform(&s,&precision,identity,0,uniform,sizeof uniform));
    for (unsigned i=0;i<12;i++)
        assert(fabsf(uniform[16+i]-values[i])<=fmaxf(1e-6f,fabsf(values[i])*.0005f));
    float invalid[12]; memcpy(invalid,identity,48); invalid[3]=65536;
    size_t bytes=sst_animation_stats().bytes;
    assert(!sst_animation_capture(2,&precision,identity,invalid,NULL,0,NULL,0));
    invalid[3]=NAN;
    assert(!sst_animation_capture(2,&precision,identity,invalid,NULL,0,NULL,0));
    assert(sst_animation_stats().bytes==bytes);
    const float boundaries[12]={0x1p-25f,nextafterf(0x1p-25f,1),0x1p-24f,0x1p-14f,
        1+0x1p-11f,1+3*0x1p-11f,-1-0x1p-11f,-1-3*0x1p-11f,
        3.5f*0x1p-24f,-3.5f*0x1p-24f,65504,-65504};
    const float rounded[12]={0,0x1p-24f,0x1p-24f,0x1p-14f,1,1+0x1p-9f,-1,-1-0x1p-9f,
        4*0x1p-24f,-4*0x1p-24f,65504,-65504};
    assert(sst_animation_capture(3,&precision,identity,identity,boundaries,48,NULL,0));
    assert(sst_animation_select(3,&precision,48,0,&s));
    assert(sst_animation_uniform(&s,&precision,identity,3,result,48));
    for (unsigned i=0;i<12;i++) assert(result[i]==rounded[i]);
    sst_animation_reset();
    // A scene unload may free the game's original palettes. Retained replay must use
    // its own packed copy; ASan also checks that no freed source buffer is read.
    SstPose retained={.anim_id=26,.anim_frame=6};
    settle(&scene,&retained);
    float *temporary=malloc(48);assert(temporary);
    memcpy(temporary,identity,48);temporary[7]=7;
    assert(sst_animation_capture(1,&retained,identity,identity,temporary,48,NULL,0));
    free(temporary);
    sst_animation_frame(NULL,NULL,++frame);
    scene.room++;
    sst_animation_frame(&scene,&retained,++frame);
    retained.pos[0]=4;retained.pos[1]=5;retained.pos[2]=6;
    assert(sst_animation_select(1,&retained,48,0,&s));
    assert(sst_animation_uniform(&s,&retained,identity,3,result,48));
    xyz(result,4,12,6);
    sst_animation_reset();
    // Native forward rolls and jump starts are finite, action-specific full-body clips.
    for (unsigned k=0;k<2;k++) {
        SstPose move={.anim_id=k?44:42,.action=k?16:12};
        sst_animation_frame(&scene,&move,++frame);
        assert(sst_animation_set_length(move.anim_id,k?7:30));
        assert(sst_animation_clip(move.anim_id,&length,&loop) && !loop);
        move.anim_frame=1;sst_animation_frame(&scene,&move,++frame);
        assert(!sst_animation_capture(1,&move,identity,identity,start_bone,48,NULL,0));
        move.anim_frame=2;sst_animation_frame(&scene,&move,++frame);start_bone[7]=2;
        assert(sst_animation_capture(1,&move,identity,identity,start_bone,48,NULL,0));
        move.anim_frame=4;sst_animation_frame(&scene,&move,++frame);start_bone[7]=4;
        assert(sst_animation_capture(1,&move,identity,identity,start_bone,48,NULL,0));
        move.anim_frame=3;assert(sst_animation_select(1,&move,48,0,&s));closef(s.blend,.5f);
        assert(sst_animation_uniform(&s,&move,identity,3,result,48));xyz(result,0,3,0);
        move.action=0;assert(!sst_animation_select(1,&move,48,0,&s));
        assert(!sst_animation_capture(2,&move,identity,identity,start_bone,48,NULL,0));
        move.action=k?16:12;move.anim_frame=6.5f;
        assert(!sst_animation_select(1,&move,48,0,&s)); // No distant tail/start borrowing.
        sst_animation_reset();
    }
    // Native airborne jumpSEd holds phase zero. Repeated draws of one source tick cannot
    // settle it, while three fresh 30 Hz samples can, independently of presentation rate.
    SstPose held={.anim_id=45,.action=16,.tick=100};
    sst_animation_frame(&scene,&held,++frame);assert(sst_animation_set_length(45,8));
    for (unsigned i=0;i<30;i++) {
        sst_animation_frame(&scene,&held,++frame);
        assert(!sst_animation_capture(1,&held,identity,identity,NULL,0,NULL,0));
    }
    held.tick=102;sst_animation_frame(&scene,&held,++frame);
    assert(!sst_animation_capture(1,&held,identity,identity,NULL,0,NULL,0));
    held.tick=103;sst_animation_frame(&scene,&held,++frame);
    assert(sst_animation_capture(1,&held,identity,identity,NULL,0,NULL,0));
    assert(sst_animation_select(1,&held,0,0,&s));
    assert(sst_animation_clip(45,&length,&loop) && length==8 && !loop);
    held.action=12;assert(!sst_animation_select(1,&held,0,0,&s));
    sst_animation_reset();held.action=16;held.tick=UINT32_MAX-1;
    sst_animation_frame(&scene,&held,++frame);held.tick=1;sst_animation_frame(&scene,&held,++frame);
    assert(sst_animation_capture(1,&held,identity,identity,NULL,0,NULL,0));
    sst_animation_reset();
    // Sprint is a loop under native action3. Other actions cannot claim a sprint pose.
    SstPose sprint={.anim_id=189,.action=3,.anim_frame=18};settle(&scene,&sprint);
    assert(sst_animation_set_length(189,24));assert(sst_animation_clip(189,&length,&loop) && loop);
    assert(sst_animation_capture(1,&sprint,identity,identity,NULL,0,NULL,0));
    assert(sst_animation_select(1,&sprint,0,0,&s));sprint.action=0;
    assert(!sst_animation_select(1,&sprint,0,0,&s));
    sprint.anim_id=43;sprint.action=12;assert(!sst_animation_clip(43,NULL,NULL));
    assert(!sst_animation_capture(1,&sprint,identity,identity,NULL,0,NULL,0));
    // The normal network playout keeps held airborne phase zero, even after its newest packet.
    SstPoseBuffer buffer;sst_pose_buffer_reset(&buffer);sst_pose_buffer_animation(&buffer,45,8,false);
    held.tick=1;held.anim_frame=0;sst_pose_buffer_push(&buffer,&held,1000);
    held.tick=2;sst_pose_buffer_push(&buffer,&held,1033);SstPose sampled;
    assert(sst_pose_buffer_sample(&buffer,1100,&sampled) && sampled.anim_frame==0);
    sst_animation_reset();
    // A rigid part can reuse its observed body joint after changing to a pose where that
    // exact geometry was not drawn. Each clip needs its own evidence from fresh ticks.
    SstPose owner={.anim_id=26,.action=0,.anim_frame=6,.tick=200};
    float joints[24];memcpy(joints,identity,48);memcpy(joints+12,identity,48);joints[19]=10;
    float rigid[12];memcpy(rigid,identity,48);rigid[7]=10;
    for(unsigned i=0;i<8;i++) {
        owner.tick++;owner.anim_frame+=1;sst_animation_frame(&scene,&owner,++frame);
        sst_animation_capture(1,&owner,identity,identity,joints,sizeof joints,NULL,0);
        sst_animation_capture(10,&owner,identity,rigid,NULL,0,NULL,0);
    }
    assert(sst_animation_stats().attachments==1);
    owner.anim_id=27;owner.action=3;
    for(unsigned i=0;i<8;i++) {
        owner.tick++;owner.anim_frame+=1;sst_animation_frame(&scene,&owner,++frame);
        sst_animation_capture(1,&owner,identity,identity,joints,sizeof joints,NULL,0);
        sst_animation_capture(10,&owner,identity,rigid,NULL,0,NULL,0);
    }
    owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    assert(sst_animation_stats().attachments==2);
    // Capture only the body for a new finite roll; its joint is visibly displaced.
    owner.anim_id=42;owner.action=12;owner.anim_frame=0;
    sst_animation_frame(&scene,&owner,++frame);sst_animation_set_length(42,30);
    joints[19]=rigid[7]=30;
    // Observe this clip's link before its capture settles, leaving no exact rigid sample.
    for(unsigned i=0;i<4;i++) {
        owner.tick++;sst_animation_frame(&scene,&owner,++frame);
        sst_animation_capture(1,&owner,identity,identity,joints,sizeof joints,NULL,0);
        sst_animation_capture(10,&owner,identity,rigid,NULL,0,NULL,0);
    }
    owner.anim_frame=2;owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    joints[19]=30;
    assert(sst_animation_capture(1,&owner,identity,identity,joints,sizeof joints,NULL,0));
    assert(sst_animation_capture(10,&owner,identity,rigid,NULL,0,NULL,0));
    assert(sst_animation_select(10,&owner,0,0,&s) && !s.attachment);
    // Selective eviction retains verified donor poses and their clip-specific associations.
    char drop_path[4096];
#ifdef _WIN32
    char rigid_temp_dir[4096];DWORD rigid_temp_size=GetTempPathA(sizeof rigid_temp_dir,rigid_temp_dir);
    assert(rigid_temp_size && rigid_temp_size<sizeof rigid_temp_dir);assert(GetTempFileNameA(rigid_temp_dir,"sst",0,drop_path));
#else
    snprintf(drop_path,sizeof drop_path,"/tmp/sst-rigid-XXXXXX");
    int drop_fd=mkstemp(drop_path);assert(drop_fd>=0);close(drop_fd);
#endif
    test_environment("SST_TEST_RIGID_DROP_FILE",drop_path);test_environment("LARECOMP_DEBUG_DATA","1");
    owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    test_environment("SST_TEST_RIGID_DROP_FILE",NULL);test_environment("LARECOMP_DEBUG_DATA",NULL);
    assert(sst_animation_select(10,&owner,0,0,&s) && s.attachment && s.joint==1);
    float attached[216]={0};owner.pos[0]=50;
    assert(sst_animation_uniform(&s,&owner,identity,0,attached,sizeof attached));xyz(attached+16,50,30,0);
    assert(!sst_animation_uniform(&s,&owner,identity,3,result,48));
    assert(sst_animation_stats().attachment_hits==1);
    // A cached body in another clip must not borrow the roll's learned attachment.
    owner.pos[0]=0;owner.anim_id=44;owner.action=16;owner.anim_frame=0;
    sst_animation_frame(&scene,&owner,++frame);
    owner.anim_frame=2;owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    assert(sst_animation_capture(1,&owner,identity,identity,joints,sizeof joints,NULL,0));
    assert(!sst_animation_select(10,&owner,0,0,&s));
    assert(sst_animation_stats().attachments==3); // The roll link is still verified.
    owner.anim_id=42;owner.action=12;owner.anim_frame=0;owner.tick++;
    sst_animation_frame(&scene,&owner,++frame);
    owner.anim_frame=2;owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    SstPose idle_owner=owner;idle_owner.anim_id=26;idle_owner.action=0;
    assert(sst_animation_skip_rigid(&idle_owner,&owner));
    assert(!sst_animation_skip_rigid(&owner,&owner));
    SstPose unseen=owner;unseen.anim_frame=29;
    assert(!sst_animation_skip_rigid(&idle_owner,&unseen));
    unseen=owner;unseen.action=0;
    assert(!sst_animation_skip_rigid(&idle_owner,&unseen));
    assert(sst_animation_stats().rigid_skips==1);
    // Live matrices still match, but a conflicting cached parent cannot supply this part.
    owner.pos[0]=0;owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    joints[19]=rigid[7]=90;
    sst_animation_capture(1,&owner,identity,identity,joints,sizeof joints,NULL,0);
    sst_animation_capture(10,&owner,identity,rigid,NULL,0,NULL,0);
    owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    assert(sst_animation_stats().attachments==2);
    assert(sst_animation_select(10,&owner,0,0,&s) && !s.attachment);
    sst_animation_reset();
    // More than four native palettes can precede a rigid draw. Retain a late,
    // larger donor while keeping the first (head) palette and the same scratch cap.
    owner=(SstPose){.anim_id=26,.anim_frame=6,.tick=300};
    float donors[6][72];
    for(unsigned b=0;b<6;b++)for(unsigned j=0;j<=b;j++) {
        memcpy(donors[b]+j*12,identity,48);
        donors[b][j*12+3]=(b+1)*10+j;
    }
    for(unsigned i=0;i<9;i++) {
        owner.tick++;owner.anim_frame+=1;sst_animation_frame(&scene,&owner,++frame);
        sst_animation_set_length(26,45);
        for(unsigned b=0;b<6;b++)
            sst_animation_capture(b+1,&owner,identity,identity,donors[b],(b+1)*48,NULL,0);
        sst_animation_capture(10,&owner,identity,donors[5]+5*12,NULL,0,NULL,0);
        sst_animation_capture(11,&owner,identity,donors[0],NULL,0,NULL,0);
    }
    owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    assert(sst_animation_stats().attachments==2);
    assert(sst_animation_select(10,&owner,0,0,&s) && !s.attachment);
    FILE *drop_file=fopen(drop_path,"w");assert(drop_file);fclose(drop_file);
    test_environment("SST_TEST_RIGID_DROP_FILE",drop_path);test_environment("LARECOMP_DEBUG_DATA","1");
    owner.tick++;sst_animation_frame(&scene,&owner,++frame);
    test_environment("SST_TEST_RIGID_DROP_FILE",NULL);test_environment("LARECOMP_DEBUG_DATA",NULL);
    assert(sst_animation_select(10,&owner,0,0,&s) && s.attachment && s.joint==5);
    assert(sst_animation_uniform(&s,&owner,identity,0,attached,sizeof attached));xyz(attached+16,65,0,0);
    assert(sst_animation_select(11,&owner,0,0,&s) && s.attachment && s.joint==0);
    assert(sst_animation_uniform(&s,&owner,identity,0,attached,sizeof attached));xyz(attached+16,10,0,0);
    assert(sst_animation_stats().bytes<=SST_ANIMATION_BUDGET);
    sst_animation_reset();
    // Verified hand variants share their wrist's rigid model pose, not opposite
    // hands or skinned layouts. A local idle hand can replay an observed roll wrist.
    owner=(SstPose){.anim_id=42,.action=12,.tick=400};
    float left_hand[12],right_hand[12];memcpy(left_hand,identity,48);memcpy(right_hand,identity,48);
    left_hand[3]=-20;left_hand[7]=80;right_hand[3]=20;right_hand[7]=90;
    for(unsigned i=0;i<8;i++) {
        owner.tick++;owner.anim_frame+=1;sst_animation_frame(&scene,&owner,++frame);
        sst_animation_set_length(42,30);
        sst_animation_capture(sst_avatar_pose_mesh(46,0,0),&owner,identity,left_hand,NULL,0,NULL,0);
        sst_animation_capture(sst_avatar_pose_mesh(47,0,0),&owner,identity,right_hand,NULL,0,NULL,0);
    }
    peer=owner;peer.pos[0]=50;
    assert(sst_animation_select(sst_avatar_pose_mesh(34,0,0),&peer,0,0,&s));
    assert(sst_animation_uniform(&s,&peer,identity,0,attached,sizeof attached));xyz(attached+16,30,80,0);
    assert(sst_animation_select(sst_avatar_pose_mesh(23,0,0),&peer,0,0,&s));
    assert(sst_animation_uniform(&s,&peer,identity,0,attached,sizeof attached));xyz(attached+16,70,90,0);
    assert(sst_animation_part_stats(34).hits==1 && sst_animation_part_stats(23).hits==1);
    assert(sst_avatar_pose_mesh(48,0,0)==sst_avatar_pose_mesh(46,0,0));
    assert(sst_avatar_pose_mesh(49,0,0)==sst_avatar_pose_mesh(47,0,0));
    assert(sst_avatar_pose_mesh(50,0,0)==23 && sst_avatar_pose_mesh(51,0,0)==23);
    assert(!sst_animation_select(sst_avatar_pose_mesh(46,48,0),&peer,48,0,&s));
    assert(!sst_animation_select(sst_avatar_pose_mesh(46,0,48),&peer,0,48,&s));
    assert(!sst_animation_select(sst_avatar_pose_mesh(21,0,0),&peer,0,0,&s));
    SstPose wrong_hand_action=peer;wrong_hand_action.action=0;
    assert(!sst_animation_select(sst_avatar_pose_mesh(34,0,0),&wrong_hand_action,0,0,&s));
    assert(sst_animation_part_stats(34).misses==1);
    scene.room++;sst_animation_frame(&scene,&owner,++frame);
    assert(sst_animation_select(sst_avatar_pose_mesh(34,0,0),&peer,0,0,&s));
    sst_animation_set_length(42,31);
    assert(!sst_animation_select(sst_avatar_pose_mesh(34,0,0),&peer,0,0,&s));
    sst_animation_reset();
    assert(!sst_animation_part_stats(34).hits && !sst_animation_part_stats(34).misses);
    assert(!sst_animation_part_stats(UINT16_MAX).hits && !sst_animation_part_stats(UINT16_MAX).misses);
    independent_replay();
    equipment_cohorts();
    child_hands_survive_rigid_sample_loss();
    puts("animation library: observed clips, independent exact/coherent neutral replay, cold-cache suppression, verified wrist/attachment/layout/action gates, retained transitions and 8 MiB cap PASS");
}

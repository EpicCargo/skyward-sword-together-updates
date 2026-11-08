#include "../src/sst_pose_buffer.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static SstPose sample(uint8_t left,uint8_t right,double tick) {
    SstPoseBuffer b;sst_pose_buffer_reset(&b);SstPose a={.tick=10,.anim_id=20,.action=3,.anim_frame=1,.equip_flags=left};
    SstPose c=a;c.tick=11;c.anim_frame=2;c.pos[0]=10;c.equip_flags=right;
    sst_pose_buffer_push(&b,&a,1000);sst_pose_buffer_push(&b,&c,1033);
    b.started=true;b.clock_tick=tick;b.last_update_ms=1033;
    SstPose out;assert(sst_pose_buffer_sample(&b,1033,&out));return out;
}
int main(void) {
    SstPose p=sample(0,SST_POSE_CONTEXT_SMALL_CARRY,10.25);assert(p.equip_flags==0 && p.anim_frame==1 && fabsf(p.pos[0]-2.5f)<.01f);
    p=sample(0,SST_POSE_CONTEXT_SMALL_CARRY,10.75);assert(p.equip_flags==SST_POSE_CONTEXT_SMALL_CARRY && p.anim_frame==2);
    p=sample(SST_POSE_CONTEXT_SMALL_CARRY,SST_POSE_CONTEXT_SMALL_CARRY,10.25);assert(fabsf(p.anim_frame-1.25f)<.001f);
    p=sample(SST_POSE_CONTEXT_SMALL_CARRY,SST_POSE_CONTEXT_SMALL_CARRY|0xe1,10.25);assert(fabsf(p.anim_frame-1.25f)<.001f);
    p=sample(SST_POSE_CONTEXT_SMALL_CARRY,SST_POSE_CONTEXT_UNSUPPORTED,10.75);assert(p.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED && p.anim_frame==2);
    p=sample(0,3,10.25);assert(p.equip_flags==0 && p.anim_frame==1 && fabsf(p.pos[0]-2.5f)<.01f);
    p=sample(0,3,10.75);assert(p.equip_flags==3 && p.anim_frame==2 && fabsf(p.pos[0]-7.5f)<.01f);
    p=sample(3,1,10.75);assert(p.equip_flags==1 && p.anim_frame==2);
    p=sample(3,3|0xe0,10.25);assert(fabsf(p.anim_frame-1.25f)<.001f);
    puts("carry playout: discrete context changes preserve source phases; position, cadence and unrelated flags unchanged PASS");return 0;
}

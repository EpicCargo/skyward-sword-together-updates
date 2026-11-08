#include "../src/sst_hand.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static const float joint[12]={2,0,0,100,0,2,0,200,0,0,2,300};
static const float local[12]={1,0,0,26.5f,0,1,0,0,0,0,1,0};
static SstPose pose={.anim_id=20,.action=3,.anim_frame=6,.tick=100};
static void observe(uint16_t mesh,const SstPose *p,const float *offset) {
    float model[12];assert(sst_hand_compose(joint,offset,model));
    sst_hand_observe(mesh,p,6,368*48,368*48,joint,model);
}
static void twice(uint16_t mesh) {
    observe(mesh,&pose,local);SstPose next=pose;next.tick++;observe(mesh,&next,local);
}
int main(void) {
    SstHandOffset got;float result[12];
    float model[12];assert(sst_hand_compose(joint,local,model));
    sst_hand_reset();
    sst_hand_observe(52,&pose,6,367*48,0,joint,model);
    sst_hand_observe(52,&pose,6,368*48,17,joint,model);
    sst_hand_observe(52,&pose,0,368*48,0,joint,model);
    assert(!sst_hand_stats().entries);
    sst_hand_reset();observe(52,&pose,local);
    for(unsigned i=0;i<5;i++)observe(52,&pose,local);
    assert(!sst_hand_lookup(52,&pose,true,40,&got)); // same source tick is not new evidence
    SstPose next=pose;next.tick++;observe(52,&next,local);
    assert(sst_hand_lookup(52,&pose,true,40,&got) && got.joint==3 && got.body==6);
    assert(sst_hand_compose(joint,got.local,result));
    assert(fabsf(result[3]-153.f)<.001f && fabsf(result[7]-200.f)<.001f);
    assert(!sst_hand_lookup(53,&pose,true,40,&got));
    SstPose wrong=pose;wrong.action=67;assert(!sst_hand_lookup(52,&wrong,true,40,&got));
    wrong=pose;wrong.anim_id=26;assert(!sst_hand_lookup(52,&wrong,true,40,&got));
    wrong=pose;wrong.anim_frame=16;assert(!sst_hand_lookup(52,&wrong,true,40,&got));
    wrong.anim_frame=NAN;assert(!sst_hand_lookup(52,&wrong,true,40,&got));
    twice(53);assert(sst_hand_lookup(53,&pose,true,40,&got) && got.joint==5);
    float shifted[12];memcpy(shifted,local,sizeof shifted);shifted[3]+=1;
    for(unsigned i=0;i<8;i++)observe(52,&next,shifted);
    assert(!sst_hand_stats().blocked_parents); // repeated presents from one tick cannot permanently block
    for(unsigned i=0;i<3;i++){next.tick++;observe(52,&next,shifted);}
    assert(!sst_hand_lookup(52,&pose,true,40,&got));
    assert(sst_hand_lookup(53,&pose,true,40,&got)); // block only the inconsistent side
    assert(sst_hand_stats().blocked_parents==1);
    sst_hand_reset();observe(52,&pose,local);
    float rotated[12];memcpy(rotated,local,sizeof rotated);rotated[0]=rotated[5]=cosf(.5f);rotated[1]=-sinf(.5f);rotated[4]=sinf(.5f);
    observe(52,&next,rotated);assert(!sst_hand_lookup(52,&pose,true,40,&got));
    assert(sst_hand_stats().blocked==1);
    sst_hand_reset();twice(52);sst_hand_forget_clip(20);
    assert(!sst_hand_lookup(52,&pose,true,40,&got) && !sst_hand_stats().entries);
    float bad[12],unchanged[12];memcpy(bad,local,sizeof bad);bad[0]=NAN;
    for(unsigned i=0;i<12;i++)result[i]=unchanged[i]=123;
    assert(!sst_hand_compose(joint,bad,result) && !memcmp(result,unchanged,sizeof result));
    memset(bad,0,sizeof bad);assert(!sst_hand_compose(joint,bad,result));
    memcpy(bad,local,sizeof bad);bad[3]=1e9;assert(!sst_hand_compose(joint,bad,result));
    sst_hand_reset();
    for(unsigned i=0;i<600;i++) {
        SstPose p=pose;p.anim_frame=i*3.f;p.tick=i*2+1;observe(52,&p,local);p.tick++;observe(52,&p,local);
    }
    assert(sst_hand_stats().entries==512 && sst_hand_stats().ready==512);
    sst_hand_reset();assert(!sst_hand_stats().entries);
    puts("hand offsets: nonidentity placement, distinct ticks, action/clip/phase/layout fencing, independent-side rejection, invalid transforms and512-entry bound passed");
}

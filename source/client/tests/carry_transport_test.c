#include "../src/sst_net.c"
#include "../src/sst_enemy_motion.h"
#include <assert.h>
int main(void) {
    const uint8_t flags[]={0,SST_POSE_CONTEXT_SMALL_CARRY,SST_POSE_CONTEXT_UNSUPPORTED,
        SST_POSE_CONTEXT_SMALL_CARRY|3|SST_MOTION_CAPABLE,SST_POSE_CONTEXT_SMALL_CARRY|0xc0};
    for(unsigned i=0;i<sizeof flags;i++) {
        SstPose p={.tick=91,.pos={1,2,3},.anim_id=20,.anim_frame=7,.action=3,.health=16,.max_health=24,.equip_flags=flags[i]};
        Buf b={0};put_pose(&b,&p);assert(b.n==36 && b.b[34]==flags[i] && b.b[35]==0 && sst_udp_pose_valid(b.b,b.n));
        Rd r={.p=b.b,.n=b.n};SstPose q={0};get_pose(&r,&q);assert(!r.bad && r.o==36 && q.equip_flags==flags[i] && q.anim_id==20 && q.anim_frame==7);
    }
    assert(sst_motion_eligible(SST_MOTION_CAPABLE|SST_POSE_CONTEXT_SMALL_CARRY,16,1000,1000));
    assert(!sst_motion_eligible(SST_MOTION_CAPABLE|SST_POSE_CONTEXT_SMALL_CARRY|0x40,16,1000,1000));
    assert(!sst_motion_eligible(SST_MOTION_CAPABLE|SST_POSE_CONTEXT_SMALL_CARRY|0x80,16,1000,1000));
    puts("carry context: protocol45/36byte codec, UDP validation and unchanged high-bit motion safety PASS");return 0;
}

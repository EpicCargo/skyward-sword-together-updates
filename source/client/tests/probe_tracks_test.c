#include "../src/sst_game.h"
#include "../src/sst_pose_probe.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {unsigned char actor[384];uint64_t player;char stage[16];uint8_t room,layer,entrance;SstGameLayout layout;} Fixture;
static unsigned checks;
static void check(bool b){checks++;assert(b);}
static bool zero(const void *p,size_t bytes){const unsigned char *s=p;for(size_t i=0;i<bytes;i++)if(s[i])return false;return true;}
static void env(const char *key,const char *value){
#ifdef _WIN32
    check(!_putenv_s(key,value?value:""));
#else
    check(!(value?setenv(key,value,1):unsetenv(key)));
#endif
}
static void put(Fixture *f,unsigned at,const void *data,size_t bytes){memcpy(f->actor+at,data,bytes);}
static void initialize(Fixture *f){
    memset(f,0,sizeof *f);f->player=(uintptr_t)f->actor;memcpy(f->stage,"F001r",6);f->room=f->layer=1;f->entrance=5;
    f->layout=(SstGameLayout){.player_ptr=(uintptr_t)&f->player,.stage_name=(uintptr_t)f->stage,.room=(uintptr_t)&f->room,.layer=(uintptr_t)&f->layer,.entrance=(uintptr_t)&f->entrance,
        .guest_lo=(uintptr_t)f,.guest_hi=(uintptr_t)f+sizeof *f,.off_pos=32,.off_rot=48,.off_action=56,.off_player_instance=8,
        .off_anim_index=64,.off_anim_ids=72,.off_anim_tracks=96,.anim_track_stride=16,.off_anim_frame=8,.off_anim_length=4,.off_carried_actor=300,.expected_player_vtable=12345};
    uint64_t vtable=12345;uint32_t instance=7;float pos[3]={10,20,30};put(f,0,&vtable,8);put(f,8,&instance,4);put(f,32,pos,12);
    for(unsigned i=0;i<6;i++){uint16_t id=26;float phase=6,length=45;put(f,72+2*i,&id,2);put(f,96+16*i+8,&phase,4);put(f,96+16*i+4,&length,4);}
}
static void rejected(Fixture *f,const SstLocal *pub,const SstPose *p){SstProbeTracks out;memset(&out,0x13,sizeof out);check(!sst_game_probe_tracks(f?&f->layout:NULL,pub,p,&out)&&zero(&out,sizeof out));}
int main(void){
    env("LARECOMP_DEBUG_DATA",NULL);env("SST_POSE_PROBE",NULL);env("SST_NO_PLAYER_READ",NULL);
    Fixture f;initialize(&f);SstLocal pub=sst_game_read_local(&f.layout,17);check(pub.valid);SstPose expected=pub.pose;
    SstGameLayout inaccessible={.player_ptr=1};SstProbeTracks out;check(!sst_game_probe_tracks(&inaccessible,&pub,&expected,&out)&&zero(&out,sizeof out));
    env("LARECOMP_DEBUG_DATA","1");rejected(&f,&pub,&expected);env("SST_POSE_PROBE","0");rejected(&f,&pub,&expected);env("SST_POSE_PROBE","1");
    unsigned char saved[sizeof f];memcpy(saved,&f,sizeof f);check(sst_game_probe_tracks(&f.layout,&pub,&expected,&out));check(!memcmp(saved,&f,sizeof f));
    check(out.primary==0&&out.valid_mask==63&&!strcmp(out.scene.stage,"F001r")&&out.scene.room==1&&out.scene.layer==1&&out.scene.entrance==5);
    for(unsigned i=0;i<6;i++)check(out.ids[i]==26&&out.frames[i]==6&&out.lengths[i]==45);
    env("SST_NO_PLAYER_READ","1");check(!sst_game_probe_tracks(&inaccessible,&pub,&expected,&out)&&zero(&out,sizeof out));env("SST_NO_PLAYER_READ",NULL);
    check(!sst_game_probe_tracks(NULL,&pub,&expected,&out));check(!sst_game_probe_tracks(&f.layout,NULL,&expected,&out));check(!sst_game_probe_tracks(&f.layout,&pub,NULL,&out));check(!sst_game_probe_tracks(&f.layout,&pub,&expected,NULL));
    pub.valid=false;rejected(&f,&pub,&expected);pub.valid=true;
    uint64_t vtable=5;put(&f,0,&vtable,8);rejected(&f,&pub,&expected);vtable=12345;put(&f,0,&vtable,8);
    uint32_t instance=8;put(&f,8,&instance,4);rejected(&f,&pub,&expected);instance=7;put(&f,8,&instance,4);
    f.player++;rejected(&f,&pub,&expected);f.player--;f.room=0;rejected(&f,&pub,&expected);f.room=1;
    f.stage[1]='2';rejected(&f,&pub,&expected);f.stage[1]='0';float phase=7;put(&f,104,&phase,4);rejected(&f,&pub,&expected);phase=6;put(&f,104,&phase,4);
    float moved=11;put(&f,32,&moved,4);rejected(&f,&pub,&expected);moved=10;put(&f,32,&moved,4);
    expected.tick++;rejected(&f,&pub,&expected);expected.tick--;uint32_t action=67;put(&f,56,&action,4);rejected(&f,&pub,&expected);action=0;put(&f,56,&action,4);
    f.actor[64]=6;rejected(&f,&pub,&expected);f.actor[64]=0;
    phase=NAN;put(&f,104,&phase,4);rejected(&f,&pub,&expected);phase=6;put(&f,104,&phase,4);
    phase=NAN;put(&f,96+4*16+8,&phase,4);check(sst_game_probe_tracks(&f.layout,&pub,&expected,&out)&&out.valid_mask==47&&out.frames[4]==0&&out.lengths[4]==0);phase=6;put(&f,96+4*16+8,&phase,4);
    SstGameLayout good=f.layout;f.layout.expected_player_vtable=0;rejected(&f,&pub,&expected);f.layout=good;f.layout.guest_lo=0;rejected(&f,&pub,&expected);f.layout=good;
    f.layout.player_ptr=1;rejected(&f,&pub,&expected);f.layout=good;f.layout.off_anim_tracks=UINT32_MAX;rejected(&f,&pub,&expected);f.layout=good;
    // A held pointer is only a presence value; no pointee is read.
    uint64_t held=1;put(&f,300,&held,8);rejected(&f,&pub,&expected);uint16_t id=431;phase=4.5f;float length=9;
    put(&f,82,&id,2);put(&f,96+5*16+8,&phase,4);put(&f,96+5*16+4,&length,4);action=67;put(&f,56,&action,4);
    pub=sst_game_read_local(&f.layout,17);expected=pub.pose;check(pub.valid&&(expected.equip_flags&SST_POSE_CONTEXT_MASK)==SST_POSE_CONTEXT_SMALL_CARRY);
    check(sst_game_probe_tracks(&f.layout,&pub,&expected,&out)&&out.ids[5]==431&&out.frames[5]==4.5f&&out.lengths[5]==9);
    memcpy(saved,&f,sizeof f);check(sst_game_probe_tracks(&f.layout,&pub,&expected,&out)&&!memcmp(saved,&f,sizeof f));
    env("LARECOMP_DEBUG_DATA",NULL);env("SST_POSE_PROBE",NULL);
    printf("probe tracks: %u checks PASS; strict opt-in, actor/scene/phase identity, bounds, immutable inputs; weights unobserved; no game\n",checks);return 0;
}

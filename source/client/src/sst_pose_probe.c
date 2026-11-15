// SPDX-License-Identifier: MIT
#include "sst_pose_probe.h"
#include "sst_avatar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(float)==4&&sizeof(SstProbeRecord)==160&&sizeof(SstProbeControls)==104,"probe layouts");
enum {PALETTE_BYTES=65536,PATH_BYTES=4096};
typedef struct {uint64_t active,last;unsigned samples;bool have_last;uint64_t meshes;SstProbeTracks tracks;SstPose pose;} Context;
static struct {
    bool started,failed,controls_mode;FILE *file;char path[PATH_BYTES];SstProbeTrackReader reader;void *user;
    Context contexts[2];uint32_t bytes,records;float scratch[12+2*PALETTE_BYTES/4];
} probe;
static bool flag(const char *key){const char *s=getenv(key);return s&&!strcmp(s,"1");}
static bool finite_data(const float *p,size_t count){for(size_t i=0;i<count;i++)if(!isfinite(p[i])||fabsf(p[i])>1e6f)return false;return true;}
static bool valid_tracks(const SstProbeTracks *t){
    if(t->primary>=6||(t->valid_mask&~63u)||!(t->valid_mask&(1u<<t->primary)))return false;
    const char *end=memchr(t->scene.stage,0,9);if(!end||end==t->scene.stage)return false;
    for(const char *s=t->scene.stage;s<end;s++)if(*s<32||*s>126)return false;
    for(unsigned i=0;i<6;i++)if(t->valid_mask&(1u<<i)){
        float f=t->frames[i],n=t->lengths[i];if(t->ids[i]>=443||!isfinite(f)||!isfinite(n)||f<0||n<=0||n>65536||f>n)return false;
    }else if(t->frames[i]!=0||t->lengths[i]!=0)return false;
    if(probe.controls_mode){
        if(!t->controls_valid)return false;
        for(unsigned i=0;i<4;i++)for(unsigned k=0;k<4;k++)if(!isfinite(t->controls.hair_quat[i][k])||fabsf(t->controls.hair_quat[i][k])>2)return false;
    }
    return true;
}
static bool equal_tracks(const SstProbeTracks *a,const SstProbeTracks *b){
    return (!probe.controls_mode||(!memcmp(&a->controls,&b->controls,sizeof a->controls)&&a->controls_valid==b->controls_valid))&&a->primary==b->primary&&a->valid_mask==b->valid_mask&&!memcmp(a->ids,b->ids,sizeof a->ids)&&
        !memcmp(a->frames,b->frames,sizeof a->frames)&&!memcmp(a->lengths,b->lengths,sizeof a->lengths)&&
        a->scene.room==b->scene.room&&a->scene.layer==b->scene.layer&&a->scene.entrance==b->scene.entrance&&!memcmp(a->scene.stage,b->scene.stage,9);
}
static bool equal_pose(const SstPose *a,const SstPose *b){
    return a->tick==b->tick&&a->anim_id==b->anim_id&&a->action==b->action&&a->anim_frame==b->anim_frame&&a->equip_flags==b->equip_flags&&
        !memcmp(a->pos,b->pos,sizeof a->pos)&&!memcmp(a->rot,b->rot,sizeof a->rot);
}
static void stop_failed(void){probe.failed=true;if(probe.file){fclose(probe.file);probe.file=NULL;}fprintf(stderr,"[sst-pose-probe] stopped after file/record limit or write failure\n");}
static void capture(uint64_t frame,uint16_t mesh,uint16_t context,const SstPose *pose,const float basis[12],const float model[12],
                    const void *positions,size_t pn,const void *normals,size_t nn,void *unused){
    (void)unused;
    if(!probe.started||probe.failed||!pose||!basis||!model||pose->anim_id!=26||mesh>=64||!mesh||
        pn>PALETTE_BYTES||nn>PALETTE_BYTES||pn%48||nn%48||(pn&&!positions)||(nn&&!normals)||
        !finite_data(basis,12)||!finite_data(model,12)||!finite_data(positions,pn/4)||!finite_data(normals,nn/4))return;
    unsigned bank;if(context==0&&pose->action==0)bank=0;
    else if(context==SST_POSE_CONTEXT_SMALL_CARRY&&pose->action==67)bank=1;else return;
    Context *c=&probe.contexts[bank];bool active=c->samples&&c->active==frame;
    if(!active&&(c->samples>=SST_PROBE_MAX_CONTEXT_SAMPLES||(c->have_last&&(frame<c->last||frame-c->last<60))))return;
    if(active&&(c->meshes&(UINT64_C(1)<<mesh)))return;
    if(active&&!equal_pose(&c->pose,pose))return;
    SstProbeTracks tracks={0};if(!probe.reader(pose,&tracks,probe.user)||!valid_tracks(&tracks)||
        tracks.ids[tracks.primary]!=pose->anim_id||fabsf(tracks.frames[tracks.primary]-pose->anim_frame)>.001f)return;
    if(probe.controls_mode&&tracks.controls.actor_yaw!=pose->rot[1])return;
    if(bank==1&&(!(tracks.valid_mask&(1u<<5))||tracks.ids[5]!=431||fabsf(tracks.frames[5]-4.5f)>.001f||fabsf(tracks.lengths[5]-9.f)>.001f))return;
    if(active&&!equal_tracks(&c->tracks,&tracks))return;
    if(!probe.file){
        uint32_t endian=UINT32_C(0x01020304);if(*(const unsigned char*)&endian!=4){stop_failed();return;}
        probe.file=fopen(probe.path,"wbx");if(!probe.file){stop_failed();return;}
        const unsigned char legacy[8]={'S','S','T','P','5','6',0,0},controls[8]={'S','S','T','C','5','8',0,0};const unsigned char *magic=probe.controls_mode?controls:legacy;uint32_t version=probe.controls_mode?2:1;
        if(fwrite(magic,1,8,probe.file)!=8||fwrite(&version,4,1,probe.file)!=1||fwrite(&endian,4,1,probe.file)!=1){stop_failed();return;}
        probe.bytes=16;
    }
    uint32_t bytes=(uint32_t)(sizeof(SstProbeRecord)+(probe.controls_mode?sizeof(SstProbeControls):0)+pn+nn);
    if(probe.records>=1024||bytes>SST_PROBE_MAX_BYTES-probe.bytes){stop_failed();return;}
    memcpy(probe.scratch,model,48);if(pn)memcpy(probe.scratch+12,positions,pn);if(nn)memcpy((unsigned char*)(probe.scratch+12)+pn,normals,nn);
    sst_avatar_matrix(probe.scratch,basis,false);
    for(size_t at=0;at<pn;at+=48)sst_avatar_matrix((float*)((unsigned char*)(probe.scratch+12)+at),basis,false);
    for(size_t at=0;at<nn;at+=48)sst_avatar_matrix((float*)((unsigned char*)(probe.scratch+12)+pn+at),basis,true);
    if(!finite_data(probe.scratch,12+(pn+nn)/4))return;
    SstProbeRecord record={.bytes=bytes,.position_bytes=(uint32_t)pn,.normal_bytes=(uint32_t)nn,.mesh=mesh,.context=context,.frame=frame,
        .tick=pose->tick,.clip=pose->anim_id,.action=pose->action,.phase=pose->anim_frame,.equipment=pose->equip_flags,.primary=tracks.primary,.valid_tracks=tracks.valid_mask,
        .room=tracks.scene.room,.layer=tracks.scene.layer,.entrance=tracks.scene.entrance};
    memcpy(record.ids,tracks.ids,sizeof record.ids);memcpy(record.frames,tracks.frames,sizeof record.frames);memcpy(record.lengths,tracks.lengths,sizeof record.lengths);
    memcpy(record.stage,tracks.scene.stage,8);memcpy(record.model,probe.scratch,48);
    if(fwrite(&record,sizeof record,1,probe.file)!=1||(probe.controls_mode&&fwrite(&tracks.controls,sizeof tracks.controls,1,probe.file)!=1)||fwrite(probe.scratch+12,1,pn+nn,probe.file)!=pn+nn||fflush(probe.file)){stop_failed();return;}
    probe.bytes+=bytes;probe.records++;
    if(!active){c->samples++;c->have_last=true;c->last=c->active=frame;c->meshes=0;c->tracks=tracks;c->pose=*pose;
        fprintf(stderr,"[sst-pose-probe] frame=%llu context=%u sample=%u primary=%u phase=%.6g tracks=%02x; weights unobserved\n",(unsigned long long)frame,context,c->samples,tracks.primary,(double)pose->anim_frame,tracks.valid_mask);}
    c->meshes|=UINT64_C(1)<<mesh;
}
bool sst_pose_probe_start(SstProbeTrackReader reader,void *user){
    if(probe.started)return false;
    if(!reader||!flag("LARECOMP_DEBUG_DATA")||!flag("SST_POSE_PROBE"))return false;
    const char *dir=getenv("SST_POSE_PROBE_DIR");if(!dir||!dir[0])return false;
    bool absolute=dir[0]=='/';
#ifdef _WIN32
    absolute=absolute||dir[0]=='\\'||(dir[0]&&dir[1]==':'&&(dir[2]=='/'||dir[2]=='\\'));
#endif
    if(!absolute)return false;
    bool controls=flag("SST_POSE_CONTROLS");int n=snprintf(probe.path,sizeof probe.path,"%s/%s",dir,controls?"native-poses-controls.bin":"native-poses.bin");if(n<0||(size_t)n>=sizeof probe.path)return false;
    probe.started=true;probe.controls_mode=controls;probe.reader=reader;probe.user=user;sst_animation_observer(capture,NULL);
    fprintf(stderr,"[sst-pose-probe] enabled; private bounded in-process capture, no replay generation\n");return true;
}
void sst_pose_probe_reset(void){
    if(probe.started){sst_animation_observer(NULL,NULL);if(probe.file)fclose(probe.file);}
    memset(&probe,0,sizeof probe);
}

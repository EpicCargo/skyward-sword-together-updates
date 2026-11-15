#include "../src/sst_pose_probe.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static unsigned checks,reads,observations,mode;static bool allow=true;
static const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static SstScene scene={.stage="F001r",.room=1,.layer=1,.entrance=5};
static SstPose pose;static char path[4096];
static void check(bool b){checks++;assert(b);}
static void env(const char *key,const char *value){
#ifdef _WIN32
    check(!_putenv_s(key,value?value:""));
#else
    check(!(value?setenv(key,value,1):unsetenv(key)));
#endif
}
static bool reader(const SstPose *p,SstProbeTracks *out,void *user){
    (void)user;reads++;if(!allow)return false;*out=(SstProbeTracks){.scene=scene,.primary=0,.valid_mask=33};out->ids[0]=26;out->frames[0]=p->anim_frame;out->lengths[0]=45;out->ids[5]=431;out->frames[5]=4.5f;out->lengths[5]=9;
    if(mode==1)out->frames[0]=NAN;if(mode==2)out->valid_mask|=128;if(mode==3)out->frames[5]+=(reads%2)*.1f;return true;
}
static void count_observer(uint64_t f,uint16_t m,uint16_t c,const SstPose *p,const float t[12],const float model[12],const void *positions,size_t pn,const void *normals,size_t nn,void *user){
    (void)f;(void)m;(void)c;(void)p;(void)t;(void)model;(void)positions;(void)pn;(void)normals;(void)nn;(void)user;observations++;
}
static void setup(uint64_t start,uint16_t context){
    sst_animation_reset();check(sst_animation_set_length(26,45));pose=(SstPose){.tick=1,.anim_id=26,.action=context?67:0,.equip_flags=(uint8_t)context,.pos={10,20,0}};
    for(unsigned i=0;i<=6;i++){pose.anim_frame=(float)i;sst_animation_frame(&scene,&pose,start+i);}
}
static bool capture(uint16_t mesh,const float view[12],const float model[12],const float position[12],const float normal[12]){
    return sst_animation_capture(mesh,&pose,view,model,position,48,normal,48);
}
static bool exists(void){struct stat s;return stat(path,&s)==0;}
static unsigned read_records(bool numeric){
    FILE *f=fopen(path,"rb");check(f!=NULL);unsigned char magic[8];uint32_t version,endian;check(fread(magic,1,8,f)==8&&!memcmp(magic,"SSTP56\0\0",8));check(fread(&version,4,1,f)==1&&version==1);check(fread(&endian,4,1,f)==1&&endian==UINT32_C(0x01020304));
    unsigned count=0;SstProbeRecord r;
    while(fread(&r,1,sizeof r,f)==sizeof r){float p[12],n[12];check(r.bytes==sizeof r+96&&r.position_bytes==48&&r.normal_bytes==48);check(fread(p,4,12,f)==12&&fread(n,4,12,f)==12);check(r.clip==26&&r.primary==0&&r.valid_tracks==33&&r.reserved==0&&!strcmp(r.stage,"F001r"));
        if(numeric){check(fabsf(r.model[3]-2)<.001f&&fabsf(r.model[7]-3)<.001f);check(fabsf(p[3]-4)<.001f&&fabsf(p[7]-5)<.001f);for(unsigned k=0;k<12;k++)check(fabsf(n[k]-identity[k])<.001f);}count++;
    }check(feof(f)&&!ferror(f));fclose(f);return count;
}
int main(int argc,char **argv){
    if(argc!=2)return 2;check(snprintf(path,sizeof path,"%s/native-poses.bin",argv[1])>0);check(!exists());
    env("LARECOMP_DEBUG_DATA",NULL);env("SST_POSE_PROBE",NULL);env("SST_POSE_PROBE_DIR",argv[1]);check(!sst_pose_probe_start(reader,NULL));
    env("LARECOMP_DEBUG_DATA","1");check(!sst_pose_probe_start(reader,NULL));env("SST_POSE_PROBE","0");check(!sst_pose_probe_start(reader,NULL));env("SST_POSE_PROBE","1");
    env("SST_POSE_PROBE_DIR","relative");check(!sst_pose_probe_start(reader,NULL)&&!exists());env("SST_POSE_PROBE_DIR",argv[1]);
    float model[12],position[12];memcpy(model,identity,48);model[3]=12;model[7]=23;memcpy(position,identity,48);position[3]=14;position[7]=25;
    // Hook observes a genuine cache hit, while normal sample storage is unchanged.
    setup(0,0);sst_animation_observer(count_observer,NULL);check(capture(6,identity,model,position,identity));SstAnimationStats before=sst_animation_stats();check(capture(6,identity,model,position,identity));SstAnimationStats after=sst_animation_stats();
    check(observations==2&&before.samples==after.samples&&before.bytes==after.bytes&&before.captures==after.captures);sst_animation_observer(NULL,NULL);
    setup(0,0);check(sst_pose_probe_start(reader,NULL));check(!sst_pose_probe_start(reader,NULL));
    unsigned char saved_model[48],saved_position[48];memcpy(saved_model,model,48);memcpy(saved_position,position,48);
    check(capture(6,identity,model,position,identity));check(capture(6,identity,model,position,identity));check(capture(7,identity,model,position,identity));
    check(!memcmp(saved_model,model,48)&&!memcmp(saved_position,position,48));sst_pose_probe_reset();check(read_records(true)==2);check(!remove(path));
    // Camera rotation/translation is stripped before private recording.
    setup(0,0);check(sst_pose_probe_start(reader,NULL));float view[12]={0,-1,0,100,1,0,0,200,0,0,1,0};
    float vm[12]={0,-1,0,77,1,0,0,212,0,0,1,0},vp[12]={0,-1,0,75,1,0,0,214,0,0,1,0},vn[12]={0,-1,0,0,1,0,0,0,0,0,1,0};
    check(capture(6,view,vm,vp,vn));sst_pose_probe_reset();check(read_records(true)==1);check(!remove(path));
    // Declared forced-viewer frames never invoke the diagnostic reader.
    setup(0,0);check(sst_pose_probe_start(reader,NULL));unsigned previous=reads;sst_animation_source_frame(&scene,&pose,6,false);check(!capture(6,identity,model,position,identity));check(reads==previous&&!exists());sst_pose_probe_reset();
    setup(0,0);allow=false;check(sst_pose_probe_start(reader,NULL));check(capture(6,identity,model,position,identity));check(!exists());sst_pose_probe_reset();allow=true;
    for(mode=1;mode<=2;mode++){setup(0,0);check(sst_pose_probe_start(reader,NULL));check(capture(6,identity,model,position,identity));check(!exists());sst_pose_probe_reset();}mode=0;
    setup(0,0);check(sst_pose_probe_start(reader,NULL));mode=3;reads=0;
    check(capture(6,identity,model,position,identity));check(capture(7,identity,model,position,identity));sst_pose_probe_reset();mode=0;check(read_records(true)==1);check(!remove(path));
    // Separate bounded sample banks for ordinary idle and steady carry.
    setup(0,0);check(sst_pose_probe_start(reader,NULL));
    for(unsigned i=0;i<18;i++){pose.anim_frame=6+(float)i;sst_animation_frame(&scene,&pose,6+60*i);check(capture(6,identity,model,position,identity));}
    setup(2000,SST_POSE_CONTEXT_SMALL_CARRY);
    for(unsigned i=0;i<18;i++){pose.anim_frame=6+(float)i;sst_animation_frame(&scene,&pose,2006+60*i);check(capture(6,identity,model,position,identity));}
    sst_pose_probe_reset();check(read_records(true)==32);
    // Existing private output is never overwritten, even across a fresh start.
    FILE *f=fopen(path,"rb");check(f!=NULL);unsigned char first[16];check(fread(first,1,16,f)==16);fclose(f);setup(0,0);check(sst_pose_probe_start(reader,NULL));check(capture(6,identity,model,position,identity));sst_pose_probe_reset();f=fopen(path,"rb");check(f!=NULL);unsigned char again[16];check(fread(again,1,16,f)==16&&!memcmp(first,again,16));fclose(f);check(read_records(false)==32);check(!remove(path));
    // Many tiny meshes hit the independent1024-record bound before byte limit.
    setup(0,0);check(sst_pose_probe_start(reader,NULL));
    for(unsigned bank=0;bank<2;bank++){
        uint64_t start=bank?4000:0;if(bank)setup(start,SST_POSE_CONTEXT_SMALL_CARRY);
        for(unsigned i=0;i<16;i++){pose.anim_frame=6+(float)i;sst_animation_frame(&scene,&pose,start+6+60*i);
            for(unsigned mesh=1;mesh<64;mesh++)check(capture((uint16_t)mesh,identity,model,position,identity));}
    }
    sst_pose_probe_reset();check(read_records(false)==1024);check(!remove(path));
    env("LARECOMP_DEBUG_DATA",NULL);env("SST_POSE_PROBE",NULL);env("SST_POSE_PROBE_DIR",NULL);sst_animation_reset();
    printf("pose probe: %u checks PASS; opt-in/private exclusive output, canonical matrices, genuine-source/cache-hit hooks, caps and immutable inputs; no game\n",checks);return 0;
}

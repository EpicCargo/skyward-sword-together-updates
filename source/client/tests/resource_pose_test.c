// Synthetic byte containers only; independent numeric expectations, no game assets.
#include "../src/sst_resource_pose.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum {BYTES=2048,CHUNK=32,DICT=128,NODE=192,CURVE=512,NAME=1792,END=1536};
static unsigned char data[BYTES];
static unsigned checks;
static void w16(unsigned at,unsigned v){data[at]=(unsigned char)(v>>8);data[at+1]=(unsigned char)v;}
static void w32(unsigned at,uint32_t v){for(unsigned i=0;i<4;i++)data[at+i]=(unsigned char)(v>>(24-i*8));}
static void wf(unsigned at,float v){uint32_t bits;memcpy(&bits,&v,4);w32(at,bits);}
static void check(bool ok){checks++;assert(ok);}
static void closef(float a,float b){check(isfinite(a) && fabsf(a-b)<.0005f);}
static bool zero(const void *p,size_t n){const unsigned char *b=p;for(size_t i=0;i<n;i++)if(b[i])return false;return true;}
static uint32_t flags(unsigned format){return 1u|(1u<<3)|(1u<<4)|(1u<<6)|(3u<<17)|(1u<<23)|(format<<27);}
static void fixture(unsigned format) {
    memset(data,0,sizeof data);memcpy(data+CHUNK,"CHR0",4);w32(CHUNK+4,END-CHUNK);w32(CHUNK+8,5);
    w32(CHUNK+16,DICT-CHUNK);w16(CHUNK+0x20,4);w16(CHUNK+0x22,1);w32(CHUNK+0x28,2);
    w32(DICT,40);w32(DICT+4,1);w32(DICT+32,NAME-DICT);w32(DICT+36,NODE-DICT);
    memcpy(data+NAME,"joint",6);w32(NODE,NAME-NODE);w32(NODE+4,flags(format));
    if(!format)wf(NODE+8,10);else w32(NODE+8,CURVE-NODE);
    wf(NODE+12,0);wf(NODE+16,0);
    if(format>=1 && format<=3) {
        w16(CURVE,2);wf(CURVE+4,.25f);
        if(format<3){wf(CURVE+8,.5f);wf(CURVE+12,10);}
        if(format==1){w32(CURVE+16,0);w32(CURVE+20,(4u<<24)|(40u<<12));}
        if(format==2){w16(CURVE+16,0);w16(CURVE+18,0);w16(CURVE+20,0);w16(CURVE+22,128);w16(CURVE+24,40);w16(CURVE+26,0);}
        if(format==3){wf(CURVE+8,0);wf(CURVE+12,10);wf(CURVE+16,0);wf(CURVE+20,4);wf(CURVE+24,30);wf(CURVE+28,0);}
    }
    if(format>=4) {
        if(format!=6){wf(CURVE,.5f);wf(CURVE+4,10);}
        for(unsigned i=0;i<=4;i++) {
            if(format==4)data[CURVE+8+i]=(unsigned char)(10*i);
            if(format==5)w16(CURVE+8+2*i,10*i);
            if(format==6)wf(CURVE+4*i,10+5*(float)i);
        }
    }
}
static SstChr0 opened(void){SstChr0 v;check(sst_chr0_open(data,sizeof data,CHUNK,&v));return v;}
static void expected_formats(void) {
    for(unsigned format=0;format<=6;format++) {
        fixture(format);SstChr0 v=opened();SstResourceSrt out;char name[128];
        check(v.duration==4 && v.joints==1 && v.scaling_rule==2);
        check(sst_chr0_joint_name(&v,0,name,sizeof name) && !strcmp(name,"joint"));
        check(sst_chr0_sample(&v,0,1,NULL,&out));closef(out.rotation[0],!format?10:format<4?13.125f:15);
        closef(out.scale[0],1);closef(out.scale[1],1);closef(out.scale[2],1);
        check(sst_chr0_sample(&v,0,1.5f,NULL,&out));
        closef(out.rotation[0],!format?10:format<4?16.328125f:17.5f);
        check(sst_chr0_sample(&v,0,0,NULL,&out));closef(out.rotation[0],10);
        check(sst_chr0_sample(&v,0,4,NULL,&out));closef(out.rotation[0],!format?10:30);
        check(out.flags==flags(format));
    }
    // Native CV data interpolates the encoded values directly. Substituting a
    // shortest-angle path would change this defined midpoint from0 to180.
    fixture(6);wf(CURVE,170);wf(CURVE+4,-170);SstChr0 v=opened();SstResourceSrt out;
    check(sst_chr0_sample(&v,0,.5f,NULL,&out));closef(out.rotation[0],0);
    // Packed negative slopes must sign-extend correctly, with their own units.
    fixture(1);w32(CURVE+16,0xfe0);w32(CURVE+20,(4u<<24)|(40u<<12)|0xfe0);
    v=opened();check(sst_chr0_sample(&v,0,1,NULL,&out));closef(out.rotation[0],12.75f);
    fixture(2);w16(CURVE+20,0xff00);w16(CURVE+26,0xff00);
    v=opened();check(sst_chr0_sample(&v,0,1,NULL,&out));closef(out.rotation[0],12.75f);
}
static void bind_identity_and_versions(void) {
    SstResourceSrt bind={{2,3,4},{10,20,30},{11,22,33},0},out;
    fixture(0);w32(NODE+4,1|(7u<<7));SstChr0 v=opened();
    check(!sst_chr0_sample(&v,0,0,NULL,&out) && zero(&out,sizeof out));
    check(sst_chr0_sample(&v,0,2,&bind,&out));
    for(unsigned i=0;i<3;i++){closef(out.scale[i],bind.scale[i]);closef(out.rotation[i],bind.rotation[i]);closef(out.translation[i],bind.translation[i]);}
    out=bind;check(sst_chr0_sample(&v,0,2,&out,&out));closef(out.translation[2],33);
    bind.translation[0]=NAN;check(!sst_chr0_sample(&v,0,0,&bind,&out) && zero(&out,sizeof out));
    w32(NODE+4,3);v=opened();check(sst_chr0_sample(&v,0,4,NULL,&out));closef(out.scale[2],1);closef(out.rotation[2],0);
    for(unsigned version=3;version<=4;version++) {
        fixture(0);w32(CHUNK+8,version);w16(CHUNK+0x1c,4);w16(CHUNK+0x1e,1);w32(CHUNK+0x20,0);w32(CHUNK+0x24,2);
        v=opened();check(sst_chr0_sample(&v,0,1,NULL,&out));closef(out.rotation[0],10);
    }
    // Constants on a formatted channel must not follow their float as a pointer.
    fixture(3);w32(NODE+4,flags(3)|(1u<<16));wf(NODE+8,-75);v=opened();
    check(sst_chr0_sample(&v,0,3,NULL,&out));closef(out.rotation[0],-75);
    // Uniform scale is one slot; subsequent groups must retain their alignment.
    fixture(0);w32(NODE+4,1|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<22));wf(NODE+8,2.5f);v=opened();
    check(sst_chr0_sample(&v,0,0,NULL,&out));for(unsigned i=0;i<3;i++)closef(out.scale[i],2.5f);
}
static void rejected(void) {SstChr0 v;memset(&v,0xa5,sizeof v);check(!sst_chr0_open(data,sizeof data,CHUNK,&v));check(zero(&v,sizeof v));}
static void malformed(void) {
    fixture(3);w32(CHUNK,0);rejected();
    fixture(3);w32(CHUNK+8,6);rejected();
    fixture(3);w32(CHUNK+4,UINT32_MAX);rejected();
    fixture(3);w32(CHUNK+16,UINT32_MAX);rejected();
    fixture(3);w32(DICT,16);rejected();
    fixture(3);w32(DICT+4,2);rejected();
    fixture(3);w16(CHUNK+0x22,257);rejected();
    fixture(3);w32(CHUNK+0x24,2);rejected();
    fixture(3);w32(CHUNK+0x28,3);rejected();
    fixture(3);w32(DICT+36,0x80000000);rejected();
    fixture(3);w32(NODE+8,END-NODE);rejected();
    fixture(3);w32(NODE+4,flags(7));rejected();
    fixture(3);w32(NODE+4,flags(3)&~1u);rejected();
    fixture(3);w16(CURVE,0);rejected();
    fixture(3);w16(CURVE,4097);rejected();
    fixture(3);wf(CURVE+4,NAN);rejected();
    fixture(3);wf(CURVE+20,-1);rejected();
    fixture(3);wf(CURVE+24,NAN);rejected(); // invalid tail rejected even at frame0
    fixture(3);wf(CURVE+28,INFINITY);rejected();
    fixture(4);wf(CURVE,NAN);rejected();
    fixture(6);wf(CURVE+16,NAN);rejected();
    fixture(3);data[NAME]=0;rejected();
    fixture(3);memset(data+NAME,'a',128);rejected();
    fixture(3);w32(NODE,NAME+1-NODE);rejected();
    fixture(3);w16(CHUNK+0x22,2);w32(DICT+4,2);w32(DICT,56);
    memcpy(data+DICT+40,data+DICT+24,16);rejected(); // duplicate joint name
    fixture(3);unsigned char backup[BYTES];memcpy(backup,data,sizeof data);
    for(size_t n=0;n<NAME+6;n++) {
        unsigned char *short_input=malloc(n?n:1);check(short_input!=NULL);memcpy(short_input,data,n);
        SstChr0 v;check(!sst_chr0_open(short_input,n,CHUNK,&v));free(short_input);
    }
    check(!memcmp(backup,data,sizeof data));
    SstChr0 v=opened();SstResourceSrt out;char name[128];
    check(!sst_chr0_sample(&v,0,-1,NULL,&out) && zero(&out,sizeof out));
    check(!sst_chr0_sample(&v,0,5,NULL,&out) && zero(&out,sizeof out));
    check(!sst_chr0_sample(&v,0,NAN,NULL,&out) && zero(&out,sizeof out));
    check(!sst_chr0_sample(&v,1,0,NULL,&out) && zero(&out,sizeof out));
    check(!sst_chr0_joint_name(&v,0,name,2) && name[0]==0);
    check(!sst_chr0_joint_name(&v,1,name,sizeof name) && name[0]==0);
    v.dictionary=SIZE_MAX;check(!sst_chr0_sample(&v,0,0,NULL,&out) && zero(&out,sizeof out));
    v=opened();v.end=SIZE_MAX;check(!sst_chr0_sample(&v,0,0,NULL,&out));
    check(!sst_chr0_open(NULL,sizeof data,CHUNK,&v));
    check(!sst_chr0_open(data,sizeof data,SIZE_MAX,&v));
    check(!sst_chr0_open(data,SST_CHR0_MAX_BYTES+1,CHUNK,&v));
    check(!sst_chr0_open(data,sizeof data,CHUNK,NULL));
    check(!sst_chr0_sample(NULL,0,0,NULL,&out) && zero(&out,sizeof out));
}
static void repeated_key_times(void) {
    // A three-key curve with two interior keys at t2. Native selection uses
    // the latter value at exactly2 and never divides by a zero-length interval.
    for(unsigned format=1;format<=3;format++) {
        fixture(format);w16(CURVE,4);
        for(unsigned i=0;i<4;i++) {
            unsigned frame=i==0?0:i==3?4:2;float value=10+10*(float)i;
            if(format==1)w32(CURVE+16+4*i,(frame<<24)|((20*i)<<12));
            if(format==2){w16(CURVE+16+6*i,frame*32);w16(CURVE+18+6*i,20*i);w16(CURVE+20+6*i,0);}
            if(format==3){wf(CURVE+8+12*i,(float)frame);wf(CURVE+12+12*i,value);wf(CURVE+16+12*i,0);}
        }
        SstChr0 v=opened();SstResourceSrt out;
        check(sst_chr0_sample(&v,0,2,NULL,&out));closef(out.rotation[0],30);
        check(sst_chr0_sample(&v,0,3,NULL,&out));closef(out.rotation[0],35);
        if(format==1)w32(CURVE+20,(20u<<12));
        if(format==2)w16(CURVE+22,0);
        if(format==3)wf(CURVE+20,0);
        v=opened();check(sst_chr0_sample(&v,0,0,NULL,&out));closef(out.rotation[0],10);
        check(sst_chr0_sample(&v,0,.001f,NULL,&out));check(out.rotation[0]>19.99f);
    }
}
static void world_hierarchy(void) {
    int16_t parent[3]={-1,0,1};SstResourceSrt pose[3]={
        {{1,1,1},{0,0,90},{10,20,0},0},{{1,1,1},{0,0,0},{2,0,0},0},{{1,1,1},{90,0,0},{0,3,0},0}};
    float out[4][12];
    check(sst_resource_pose_world(parent,pose,3,out,4));
    closef(out[1][3],10);closef(out[1][7],22);closef(out[2][3],7);closef(out[2][7],22);
    closef(out[2][0],0);closef(out[2][2],1);closef(out[2][9],1);check(zero(out[3],sizeof out[3]));
    pose[0].rotation[2]=0;pose[0].rotation[1]=90;
    check(sst_resource_pose_world(parent,pose,3,out,4));closef(out[1][3],10);closef(out[1][11],-2);
    parent[1]=1;check(!sst_resource_pose_world(parent,pose,3,out,4) && zero(out,sizeof out));parent[1]=0;
    parent[0]=-2;check(!sst_resource_pose_world(parent,pose,3,out,4) && zero(out,sizeof out));parent[0]=-1;
    pose[2].scale[0]=2;check(!sst_resource_pose_world(parent,pose,3,out,4) && zero(out,sizeof out));pose[2].scale[0]=1;
    pose[2].rotation[0]=NAN;check(!sst_resource_pose_world(parent,pose,3,out,4) && zero(out,sizeof out));pose[2].rotation[0]=0;
    pose[2].translation[1]=INFINITY;check(!sst_resource_pose_world(parent,pose,3,out,4) && zero(out,sizeof out));pose[2].translation[1]=3;
    check(!sst_resource_pose_world(parent,pose,0,out,4) && zero(out,sizeof out));
    check(!sst_resource_pose_world(parent,pose,4,out,3) && zero(out,3*sizeof out[0]));
    check(!sst_resource_pose_world(NULL,pose,3,out,4) && zero(out,sizeof out));
    check(!sst_resource_pose_world(parent,NULL,3,out,4) && zero(out,sizeof out));
    // Individually bounded translations can still overflow the admitted world range.
    pose[0].rotation[1]=0;pose[0].translation[0]=1e6f;pose[1].translation[0]=1;
    check(!sst_resource_pose_world(parent,pose,3,out,4) && zero(out,sizeof out));
}
static uint32_t random_state=UINT32_C(0x531a47c9);
static uint32_t random_word(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void corrupted_spans(void) {
    for(unsigned trial=0;trial<1024;trial++) {
        fixture(trial%7);
        size_t n=trial%3?BYTES:random_word()%(BYTES+1);
        unsigned char *input=malloc(n?n:1),saved[BYTES];check(input!=NULL);memcpy(input,data,n);
        for(unsigned k=0;k<3 && n;k++)input[random_word()%n]^=(unsigned char)(1u<<(random_word()%8));
        memcpy(saved,input,n);SstChr0 view;
        if(sst_chr0_open(input,n,CHUNK,&view)) {
            SstResourceSrt bind={{1,1,1},{0,0,0},{0,0,0},0},out;
            for(unsigned i=0;i<view.joints;i++) {
                char name[128];check(sst_chr0_joint_name(&view,i,name,sizeof name));
                if(sst_chr0_sample(&view,i,view.duration*.5f,&bind,&out)) {
                    for(unsigned axis=0;axis<3;axis++)check(isfinite(out.scale[axis]) && isfinite(out.rotation[axis]) && isfinite(out.translation[axis]));
                } else check(zero(&out,sizeof out));
            }
        } else check(zero(&view,sizeof view));
        check(!memcmp(saved,input,n));free(input);
    }
}
int main(void) {
    expected_formats();bind_identity_and_versions();malformed();repeated_key_times();world_hierarchy();corrupted_spans();
    printf("resource pose: %u checks PASS; seven formats, native interpolation, repeated times, defaults, unit hierarchy, bounds and1024corrupted spans; no game or rendering acceptance\n",checks);
    return 0;
}

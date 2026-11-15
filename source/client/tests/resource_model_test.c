// Independent synthetic two-bone skinning oracles. No game resources.
#include "../src/sst_resource_model.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum{B=8192,M=32,INFO=M+0x4c,GRAPH=256,TABLE=384,BONES=512,TREE=1024,MIX=1100,DRAW=1300,N0=1600,N1=1808,END=7000};
static unsigned char data[B],animation[2048];static unsigned strings,checks;
static const float id[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static void w16(unsigned char *p,unsigned at,unsigned n){p[at]=(unsigned char)(n>>8);p[at+1]=(unsigned char)n;}
static void w32(unsigned char *p,unsigned at,uint32_t n){for(unsigned i=0;i<4;i++)p[at+i]=(unsigned char)(n>>(24-8*i));}
static void wf(unsigned char *p,unsigned at,float f){uint32_t u;memcpy(&u,&f,4);w32(p,at,u);}
static void matrix(unsigned at,float x,float y){for(unsigned i=0;i<12;i++)wf(data,at+4*i,id[i]);wf(data,at+12,x);wf(data,at+28,y);}
static unsigned label(const char *s){unsigned at=strings;size_t n=strlen(s)+1;memcpy(data+at,s,n);strings+=(unsigned)n;return at;}
static void dic(unsigned at,unsigned n){w32(data,at,24+16*n);w32(data,at+4,n);}
static void ent(unsigned at,unsigned index,const char *name,unsigned payload){unsigned e=at+24+16*index;w32(data,e+8,label(name)-at);w32(data,e+12,payload-at);}
static unsigned op(unsigned at,unsigned code,unsigned a,unsigned b){data[at]=(unsigned char)code;w16(data,at+1,a);w16(data,at+3,b);return at+5;}
static unsigned blend(unsigned at,unsigned dst,float a,float b){data[at]=3;w16(data,at+1,dst);data[at+3]=2;w16(data,at+4,0);wf(data,at+6,a);w16(data,at+10,3);wf(data,at+12,b);return at+16;}
static void bone(unsigned at,unsigned index,const char *name,unsigned slot,int parent,float x,float y,float fx,float fy){
    w32(data,at,0xd0);w32(data,at+4,(uint32_t)(M-at));w32(data,at+8,label(name)-at);w32(data,at+12,index);w32(data,at+16,slot);
    for(unsigned a=0;a<3;a++)wf(data,at+0x20+4*a,1);wf(data,at+0x38,x);wf(data,at+0x3c,y);
    w32(data,at+0x5c,parent<0?0:(uint32_t)(N0-at));matrix(at+0x70,fx,fy);matrix(at+0xa0,-fx,-fy);
}
static void fixture(void){
    memset(data,0,sizeof data);strings=7500;memcpy(data+M,"MDL0",4);w32(data,M+4,END-M);w32(data,M+8,11);
    w32(data,M+16,GRAPH-M);w32(data,M+20,BONES-M);w32(data,M+0x48,label("model")-M);
    w32(data,INFO,0x40);w32(data,INFO+4,(uint32_t)(M-INFO));w32(data,INFO+8,2);w32(data,INFO+28,3);data[INFO+32]=1;
    w32(data,INFO+36,TABLE-INFO);w32(data,TABLE,4);w32(data,TABLE+4,0);w32(data,TABLE+8,UINT32_MAX);w32(data,TABLE+12,UINT32_MAX);w32(data,TABLE+16,1);
    dic(GRAPH,3);ent(GRAPH,0,"NodeTree",TREE);ent(GRAPH,1,"NodeMix",MIX);ent(GRAPH,2,"DrawOpa",DRAW);data[DRAW]=1;
    dic(BONES,2);ent(BONES,0,"root",N0);ent(BONES,1,"tip",N1);
    bone(N0,0,"root",0,-1,10,20,10,20);bone(N1,1,"tip",3,0,2,0,12,20);
    unsigned at=op(TREE,2,0,0);at=op(at,2,1,0);data[at]=1;
    at=op(MIX,5,0,0);at=op(at,5,3,1);at=blend(at,1,.25f,.75f);at=blend(at,2,.5f,.5f);data[at]=1;
}
static void check(bool b){checks++;assert(b);}
static bool zero(const void *p,size_t n){const unsigned char *s=p;for(size_t i=0;i<n;i++)if(s[i])return false;return true;}
static void near(float a,float b){check(isfinite(a)&&fabsf(a-b)<.00001f);}
static SstMdl0 opened(void){SstMdl0 v;check(sst_mdl0_open(data,sizeof data,M,&v));return v;}
static void worlds(float out[2][12],bool posed){memcpy(out[0],id,sizeof id);out[0][3]=10;out[0][7]=20;memcpy(out[1],out[0],sizeof id);out[1][3]=12;if(posed){out[1][0]=out[1][5]=0;out[1][1]=-1;out[1][4]=1;}}
static void numeric_palettes(void){
    fixture();SstMdl0 m=opened();SstModelNode node;float w[2][12],p[5][12],n[5][12];
    check(m.nodes==2&&m.world_matrices==4&&m.view_matrices==3&&!strcmp(m.name,"model"));check(sst_mdl0_node(&m,1,&node)&&node.parent==0&&node.matrix==3&&!strcmp(node.name,"tip"));
    worlds(w,false);check(sst_mdl0_palettes(&m,w,2,p,n,5));
    for(unsigned k=0;k<12;k++){near(p[0][k],w[0][k]);near(p[1][k],id[k]);near(p[2][k],id[k]);near(n[1][k],k%4==3?0:id[k]);}
    check(zero(p[3],2*sizeof p[0])&&zero(n[3],2*sizeof n[0]));
    worlds(w,true);check(sst_mdl0_palettes(&m,w,2,p,n,5));
    near(p[1][0],.25f);near(p[1][1],-.75f);near(p[1][4],.75f);near(p[1][5],.25f);near(p[1][3],24);near(p[1][7],6);
    near(p[2][0],.5f);near(p[2][1],-.5f);near(p[2][3],16);near(p[2][7],4);
    // NORMAL-mode normals use the weighted linear matrix, not its inverse transpose.
    for(unsigned i=0;i<3;i++)for(unsigned k=0;k<12;k++)near(n[i][k],k%4==3?0:p[i][k]);
    // Accept bounded authored rounding, preserving it rather than renormalizing.
    wf(data,MIX+16,.250005f);m=opened();check(sst_mdl0_palettes(&m,w,2,p,n,5));check(fabsf(p[1][0]-.250005f)<.0000002f);
    fixture();w32(data,TABLE,5);w32(data,TABLE+20,0);w32(data,INFO+28,5);unsigned at=op(TREE+10,6,4,0);data[at]=1;
    m=opened();check(sst_mdl0_palettes(&m,w,2,p,n,5));for(unsigned k=0;k<12;k++)near(p[4][k],p[0][k]);
}
static SstChr0 anim(void){
    memset(animation,0,sizeof animation);unsigned char *a=animation;memcpy(a+32,"CHR0",4);w32(a,36,1504);w32(a,40,5);w32(a,48,96);w16(a,64,4);w16(a,66,1);w32(a,72,2);
    w32(a,128,40);w32(a,132,1);w32(a,160,1792-128);w32(a,164,192-128);memcpy(a+1792,"tip",4);w32(a,192,1792-192);
    uint32_t flags=1|(1u<<3)|(1u<<4)|(1u<<9)|(3u<<16)|(1u<<23)|(6u<<27);w32(a,196,flags);wf(a,200,0);wf(a,204,0);w32(a,208,512-192);
    for(unsigned i=0;i<=4;i++)wf(a,512+4*i,45*(float)i);SstChr0 v;check(sst_chr0_open(a,sizeof animation,32,&v));return v;
}
static void binding_pose(void){
    fixture();SstMdl0 m=opened();SstChr0 a=anim();SstModelBinding b;float j[3][12],p[4][12],n[4][12];
    check(sst_model_bind(&m,&a,&b)&&b.matched==1&&b.tracks[0]==-1&&b.tracks[1]==0);
    check(sst_model_pose(&b,2,j,3,p,n,4));near(j[1][3],12);near(j[1][7],20);near(p[1][3],24);near(p[1][7],6);near(n[1][0],.25f);check(zero(j[2],sizeof j[2]));
    check(!sst_model_pose(&b,NAN,j,3,p,n,4)&&zero(j,sizeof j)&&zero(p,sizeof p)&&zero(n,sizeof n));
    check(!sst_model_pose(&b,5,j,3,p,n,4)&&zero(j,sizeof j));
    check(!sst_model_pose(&b,2,j,1,p,n,4)&&zero(j,sizeof j[0])&&zero(p,sizeof p)&&zero(n,sizeof n));
    b.tracks[0]=0;check(!sst_model_pose(&b,2,j,3,p,n,4)&&zero(j,sizeof j));b.tracks[0]=-1;
    b.tracks[1]=-2;check(!sst_model_pose(&b,2,j,3,p,n,4)&&zero(j,sizeof j));
    SstModelNode bad;memset(&bad,1,sizeof bad);check(!sst_mdl0_node(&m,2,&bad)&&zero(&bad,sizeof bad));
}
static void rejected(void){SstMdl0 m;memset(&m,0x7f,sizeof m);check(!sst_mdl0_open(data,sizeof data,M,&m)&&zero(&m,sizeof m));}
static void malformed(void){
    fixture();w32(data,M+8,10);rejected();fixture();w32(data,M+4,UINT32_MAX);rejected();fixture();w32(data,INFO+28,5);rejected();
    fixture();w32(data,TABLE,1025);rejected();fixture();w32(data,TABLE,0);rejected();fixture();data[INFO+35]=1;rejected();fixture();data[INFO+35]=2;rejected();
    fixture();w32(data,N0+24,1);rejected();fixture();wf(data,N1+0x20,2);rejected();fixture();wf(data,N1+0xa0,NAN);rejected();
    fixture();wf(data,N1+0xa0+12,0);rejected();fixture();w32(data,N1+0x5c,1);rejected();fixture();w32(data,N0+16,3);rejected();
    fixture();w32(data,N1+12,0);rejected();fixture();w32(data,GRAPH+36,UINT32_MAX);rejected();fixture();data[TREE]=0xff;rejected();
    fixture();w16(data,TREE+8,3);rejected();fixture();w16(data,MIX+3,255);rejected();fixture();w16(data,MIX+14,2);rejected();
    fixture();data[MIX+13]=0;rejected();fixture();data[MIX+13]=17;rejected();fixture();wf(data,MIX+16,NAN);rejected();
    fixture();wf(data,MIX+16,-.25f);rejected();fixture();wf(data,MIX+16,.4f);rejected();fixture();w16(data,MIX+20,0);rejected();
    fixture();w16(data,MIX+28,1);rejected();fixture();memset(data+MIX,0,DRAW-MIX);rejected();
    fixture();unsigned char before[B];memcpy(before,data,B);
    for(size_t size=0;size<strings;size++){unsigned char *short_input=malloc(size?size:1);check(short_input!=NULL);memcpy(short_input,data,size);SstMdl0 m;check(!sst_mdl0_open(short_input,size,M,&m));free(short_input);}
    check(!memcmp(data,before,B));
}
static void output_admission(void){
    fixture();SstMdl0 m=opened();float w[2][12],p[5][12],n[5][12];worlds(w,true);
    memset(p,1,sizeof p);memset(n,1,sizeof n);
    check(!sst_mdl0_palettes(&m,NULL,2,p,n,5)&&zero(p,sizeof p)&&zero(n,sizeof n));
    check(!sst_mdl0_palettes(&m,w,1,p,n,5)&&zero(p,sizeof p)&&zero(n,sizeof n));
    w[1][0]=NAN;check(!sst_mdl0_palettes(&m,w,2,p,n,5)&&zero(p,sizeof p)&&zero(n,sizeof n));worlds(w,true);w[1][0]=2;
    check(!sst_mdl0_palettes(&m,w,2,p,n,5)&&zero(p,sizeof p));worlds(w,true);
    check(!sst_mdl0_palettes(&m,w,2,p,n,2)&&zero(p,2*sizeof p[0]));
    memset(p,0x13,sizeof p);float saved[5][12];memcpy(saved,p,sizeof p);check(!sst_mdl0_palettes(&m,w,2,p,p,5)&&!memcmp(saved,p,sizeof p));
    unsigned char original[B];memcpy(original,data,B);
    check(!sst_mdl0_palettes(&m,w,2,(float(*)[12])(void*)data,n,5)&&!memcmp(original,data,B));
    wf(data,MIX+16,NAN);check(!sst_mdl0_palettes(&m,w,2,p,n,5)&&zero(p,sizeof p)&&zero(n,sizeof n));
    m.bones=SIZE_MAX;SstModelNode node;check(!sst_mdl0_node(&m,0,&node)&&zero(&node,sizeof node));
}
static uint32_t seed=UINT32_C(0x541e02f9);
static uint32_t random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void corrupted(void){
    for(unsigned trial=0;trial<1024;trial++){
        fixture();size_t size=trial%3?B:random_word()%(B+1);unsigned char *input=malloc(size?size:1),saved[B];check(input!=NULL);memcpy(input,data,size);
        for(unsigned k=0;k<3&&size;k++)input[random_word()%size]^=(unsigned char)(1u<<(random_word()%8));memcpy(saved,input,size);
        SstMdl0 m;if(sst_mdl0_open(input,size,M,&m)){
            float w[SST_CHR0_MAX_JOINTS][12],p[SST_MDL0_MAX_MATRICES][12],n[SST_MDL0_MAX_MATRICES][12];
            for(unsigned i=0;i<m.nodes;i++){SstModelNode node;check(sst_mdl0_node(&m,i,&node));memcpy(w[i],node.forward_bind,sizeof w[i]);}
            bool ok=sst_mdl0_palettes(&m,w,m.nodes,p,n,SST_MDL0_MAX_MATRICES);
            if(ok)for(unsigned i=0;i<m.view_matrices;i++)for(unsigned k=0;k<12;k++)check(isfinite(p[i][k])&&isfinite(n[i][k]));
            else check(zero(p,sizeof p)&&zero(n,sizeof n));
        }else check(zero(&m,sizeof m));check(!memcmp(saved,input,size));free(input);
    }
}
int main(void){numeric_palettes();binding_pose();malformed();output_admission();corrupted();printf("resource model: %u checks PASS; weighted palettes, native NORMAL normals, binding, aliases, bounds and1024corrupted spans; no live rendering acceptance\n",checks);return 0;}

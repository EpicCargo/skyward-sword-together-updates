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
    // Link changes the model to APPROX at runtime. With a unit-scale root this
    // preserves the same palettes; no resource mutation or mode override is needed.
    float saved_p[5][12],saved_n[5][12];memcpy(saved_p,p,sizeof p);memcpy(saved_n,n,sizeof n);
    data[INFO+35]=1;m=opened();check(m.envelope_mode==1&&sst_mdl0_palettes(&m,w,2,p,n,5));
    check(!memcmp(saved_p,p,sizeof p)&&!memcmp(saved_n,n,sizeof n));
    m.envelope_mode=0;check(!sst_mdl0_palettes(&m,w,2,p,n,5)&&zero(p,sizeof p)&&zero(n,sizeof n));
    data[INFO+35]=0;
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
static SstChr0 constant_anim(unsigned char *a,const char *name,float xrot,float yrot,float zrot,float tx,unsigned patches){
    memset(a,0,2048);memcpy(a+32,"CHR0",4);w32(a,36,1504);w32(a,40,5);w32(a,48,96);w16(a,64,4);w16(a,66,1);w32(a,72,2);
    w32(a,128,40);w32(a,132,1);w32(a,160,1792-128);w32(a,164,192-128);memcpy(a+1792,name,strlen(name)+1);w32(a,192,1792-192);
    unsigned flags=1|(1u<<3)|(1u<<4)|patches,at=200;
    if(!(patches&(1u<<8)))flags|=(7u<<16)|(1u<<23);
    if(!(patches&(1u<<9)))flags|=(7u<<19)|(1u<<24);
    w32(a,196,flags);
    if(!(patches&(1u<<8))){wf(a,at,xrot);wf(a,at+4,yrot);wf(a,at+8,zrot);at+=12;}
    if(!(patches&(1u<<9))){wf(a,at,tx);wf(a,at+4,0);wf(a,at+8,0);}
    SstChr0 view;check(sst_chr0_open(a,2048,32,&view));return view;
}
static void layers_numeric(void){
    fixture();SstMdl0 m=opened();unsigned char a[2048],b[2048],c[2048];
    SstChr0 ca=constant_anim(a,"tip",0,0,0,2,0),cb=constant_anim(b,"tip",0,0,90,6,0);
    SstModelBinding ba,bb,bc;check(sst_model_bind(&m,&ca,&ba)&&sst_model_bind(&m,&cb,&bb));
    SstModelLayer layers[6]={{&ba,NULL,0,0,1},{&bb,NULL,0,0,1}};float j[3][12],p[4][12],n[4][12];
    check(sst_model_layers(&m,layers,2,j,3,p,n,4));near(j[1][3],14);near(j[1][7],20);
    near(j[1][0],sqrtf(.5f));near(j[1][4],sqrtf(.5f));near(j[0][3],10);check(zero(j[2],sizeof j[2]));
    layers[1].weight=3;check(sst_model_layers(&m,layers,2,j,3,p,n,4));near(j[1][3],15);
    near(j[1][0],.3826834324f);near(j[1][4],.9238795325f);
    // Masks exclude a contribution without changing the other joint's weights.
    uint8_t mask[2]={1,0};layers[1].mask=mask;layers[1].mask_count=2;
    check(sst_model_layers(&m,layers,2,j,3,p,n,4));near(j[1][3],12);near(j[1][0],1);
    mask[1]=1;layers[1].weight=0;layers[1].frame=NAN;
    check(sst_model_layers(&m,layers,2,j,3,p,n,4));near(j[1][3],12);
    // A single active channel must avoid a quaternion round-trip, preserving FIdx.
    ca=constant_anim(a,"tip",0,0,.703125f,2,0);check(sst_model_bind(&m,&ca,&ba));
    check(sst_model_layers(&m,layers,1,j,3,p,n,4));check(fabsf(j[1][4]-.0122705f)<1e-8f);
    // Shortest quaternion hemisphere, and all three largest-diagonal branches.
    ca=constant_anim(a,"tip",0,0,170,2,0);cb=constant_anim(b,"tip",0,0,-170,2,0);
    check(sst_model_bind(&m,&ca,&ba)&&sst_model_bind(&m,&cb,&bb));layers[1]=(SstModelLayer){&bb,NULL,0,0,1};
    check(sst_model_layers(&m,layers,2,j,3,p,n,4));near(j[1][0],-1);near(j[1][5],-1);near(j[1][4],0);
    for(unsigned axis=0;axis<3;axis++){
        ca=constant_anim(a,"tip",axis==0?180:0,axis==1?180:0,axis==2?180:0,2,0);cb=constant_anim(b,"tip",axis==0?180:0,axis==1?180:0,axis==2?180:0,2,0);
        check(sst_model_bind(&m,&ca,&ba)&&sst_model_bind(&m,&cb,&bb));check(sst_model_layers(&m,layers,2,j,3,p,n,4));
        for(unsigned k=0;k<3;k++)near(j[1][5*k],axis==k?1:-1);
    }
    ca=constant_anim(a,"tip",0,0,0,2,0);cb=constant_anim(b,"tip",0,0,.001f,2,0);
    check(sst_model_bind(&m,&ca,&ba)&&sst_model_bind(&m,&cb,&bb));check(sst_model_layers(&m,layers,2,j,3,p,n,4));check(j[1][4]>0&&j[1][4]<.00002f);
    // Three noncommuting inputs: native sequential slerp is order-sensitive.
    ca=constant_anim(a,"tip",0,0,0,2,0);cb=constant_anim(b,"tip",0,0,90,4,0);SstChr0 cc=constant_anim(c,"tip",90,0,0,6,0);
    check(sst_model_bind(&m,&ca,&ba)&&sst_model_bind(&m,&cb,&bb)&&sst_model_bind(&m,&cc,&bc));layers[2]=(SstModelLayer){&bc,NULL,0,0,1};
    check(sst_model_layers(&m,layers,3,j,3,p,n,4));float ordered[12];memcpy(ordered,j[1],sizeof ordered);
    // Closed-form slerp(z45,x90,1/3), independently evaluated in float64.
    const float expected[9]={.8500034014f,-.5065825605f,.1444587379f,.5065825605f,.7108780669f,-.4878795789f,.1444587379f,.4878795789f,.8608746654f};
    for(unsigned row=0;row<3;row++)for(unsigned col=0;col<3;col++)near(ordered[4*row+col],expected[3*row+col]);
    SstModelLayer tmp=layers[0];layers[0]=layers[2];layers[2]=tmp;check(sst_model_layers(&m,layers,3,j,3,p,n,4));
    float difference=0;for(unsigned k=0;k<12;k++)difference=fmaxf(difference,fabsf(j[1][k]-ordered[k]));check(difference>.01f);near(j[1][3],14);
    // Six layers normalize independently at each joint, omitting absent tracks.
    for(unsigned l=0;l<6;l++)layers[l]=(SstModelLayer){l%2?&bb:&ba,NULL,0,0,1};
    check(sst_model_layers(&m,layers,6,j,3,p,n,4));near(j[1][3],13);near(j[0][3],10);
}
static void layers_patch_defaults(void){
    fixture();SstMdl0 m=opened();unsigned char a[2048],b[2048];
    SstChr0 ca=constant_anim(a,"root",0,0,0,0,(1u<<8)|(1u<<9)),cb=constant_anim(b,"root",0,0,0,2,0);
    SstModelBinding ba,bb;check(sst_model_bind(&m,&ca,&ba)&&sst_model_bind(&m,&cb,&bb));
    SstModelLayer layers[2]={{&ba,NULL,0,0,1},{&bb,NULL,0,0,1}};float j[2][12],p[3][12],n[3][12];
    // Mixed model-default/explicit translation uses raw identity THEN blending;
    // pre-patching bind would incorrectly produce6 instead of1 on root.x.
    check(sst_model_layers(&m,layers,2,j,2,p,n,3));near(j[0][3],1);near(j[0][7],0);near(j[1][3],3);
    layers[1].binding=&ba;check(sst_model_layers(&m,layers,2,j,2,p,n,3));near(j[0][3],10);near(j[0][7],20);
    // A rotated bind distinguishes patch-after-blend from pre-patched slerp.
    fixture();wf(data,N1+0x34,90);const float forward[12]={0,-1,0,12,1,0,0,20,0,0,1,0},inverse[12]={0,1,0,-20,-1,0,0,12,0,0,1,0};
    for(unsigned k=0;k<12;k++){wf(data,N1+0x70+4*k,forward[k]);wf(data,N1+0xa0+4*k,inverse[k]);}m=opened();
    ca=constant_anim(a,"tip",0,0,0,0,(1u<<8)|(1u<<9));cb=constant_anim(b,"tip",0,0,0,2,0);
    check(sst_model_bind(&m,&ca,&ba)&&sst_model_bind(&m,&cb,&bb));layers[1].binding=&bb;
    check(sst_model_layers(&m,layers,2,j,2,p,n,3));near(j[1][0],1);near(j[1][4],0);near(j[1][3],11);
    layers[1].binding=&ba;check(sst_model_layers(&m,layers,2,j,2,p,n,3));near(j[1][0],0);near(j[1][4],1);near(j[1][3],12);
    check(sst_model_layers(&m,NULL,0,j,2,p,n,3));near(j[1][4],1);near(j[1][3],12);
}
static void layers_rejection_and_aliases(void){
    fixture();SstMdl0 m=opened();SstChr0 a=anim();SstModelBinding b;check(sst_model_bind(&m,&a,&b));
    SstModelLayer layer={&b,NULL,0,2,1};float j[3][12],p[4][12],n[4][12];
    check(!sst_model_layers(&m,NULL,1,j,3,p,n,4)&&zero(j,sizeof j)&&zero(p,sizeof p)&&zero(n,sizeof n));
    layer.weight=NAN;check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(j,sizeof j));layer.weight=-1;
    check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(p,sizeof p));layer.weight=1;layer.frame=5;
    check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(n,sizeof n));layer.frame=2;
    uint8_t mask[2]={1,2};layer.mask=mask;layer.mask_count=2;check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(j,sizeof j));
    mask[1]=1;layer.mask_count=1;check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(j,sizeof j));layer.mask_count=2;
    b.tracks[1]=-2;check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(j,sizeof j));b.tracks[1]=0;b.tracks[0]=0;
    check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(j,sizeof j));b.tracks[0]=-1;b.model.offset++;
    check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(j,sizeof j));b.model=m;
    // Explicit non-unit animation scale rejects, including when another layer is unit.
    w32(animation,196,1|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<22));wf(animation,200,2);check(sst_chr0_open(animation,sizeof animation,32,&a));check(sst_model_bind(&m,&a,&b));
    check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&zero(j,sizeof j)&&zero(p,sizeof p));
    layer.weight=0;check(sst_model_layers(&m,&layer,1,j,3,p,n,4));near(j[1][3],12);
    a=anim();check(sst_model_bind(&m,&a,&b));layer=(SstModelLayer){&b,NULL,0,2,1};
    SstModelLayer empty={0};check(sst_model_layers(&m,&empty,1,j,3,p,n,4));near(j[1][3],12);
    empty.weight=1;check(!sst_model_layers(&m,&empty,1,j,3,p,n,4)&&zero(j,sizeof j));
    // Buffer extents and input aliases reject without touching their bytes.
    memset(j,0x21,sizeof j);float saved_j[3][12];memcpy(saved_j,j,sizeof j);
    check(!sst_model_layers(&m,&layer,7,j,3,p,n,4)&&!memcmp(saved_j,j,sizeof j));
    layer.mask=(const uint8_t*)j;layer.mask_count=2;
    check(!sst_model_layers(&m,&layer,1,j,3,p,n,4)&&!memcmp(saved_j,j,sizeof j));layer.mask=NULL;layer.mask_count=0;
    unsigned char original[B];memcpy(original,data,sizeof data);
    check(!sst_model_layers(&m,&layer,1,(float(*)[12])(void*)data,3,p,n,4)&&!memcmp(original,data,sizeof data));
    union {SstModelLayer layer;float floats[3][12];} shared;memset(&shared,0,sizeof shared);shared.layer=layer;unsigned char saved[sizeof shared];memcpy(saved,&shared,sizeof shared);
    check(!sst_model_layers(&m,&shared.layer,1,shared.floats,3,p,n,4)&&!memcmp(saved,&shared,sizeof shared));
    union {SstModelBinding binding;float floats[3][12];} borrowed;memset(&borrowed,0,sizeof borrowed);borrowed.binding=b;layer.binding=&borrowed.binding;
    unsigned char saved_binding[sizeof borrowed];memcpy(saved_binding,&borrowed,sizeof borrowed);
    check(!sst_model_layers(&m,&layer,1,borrowed.floats,3,p,n,4)&&!memcmp(saved_binding,&borrowed,sizeof borrowed));
}
static void linked_joint_callback(void){
    fixture();SstMdl0 m=opened();float source[2][12],j[3][12],p[4][12],n[4][12];worlds(source,true);SstModelJointLink links[2]={{1,1},{0,0}};
    check(sst_model_linked_pose(&m,source,2,links,1,j,3,p,n,4));check(!memcmp(j[1],source[1],sizeof j[1]));near(p[1][3],24);near(p[1][7],6);check(zero(j[2],sizeof j[2]));
    // Callback overwrites absolute joints after hierarchy evaluation, not parents.
    links[0]=(SstModelJointLink){0,1};source[0][0]=NAN;
    check(sst_model_linked_pose(&m,source,2,links,1,j,3,p,n,4));check(!memcmp(j[0],source[1],sizeof j[0]));near(j[1][3],12);near(j[1][0],1);
    links[1]=links[0];check(!sst_model_linked_pose(&m,source,2,links,2,j,3,p,n,4)&&zero(j,sizeof j)&&zero(p,sizeof p)&&zero(n,sizeof n));
    links[0].target=2;check(!sst_model_linked_pose(&m,source,2,links,1,j,3,p,n,4)&&zero(j,sizeof j));links[0]=(SstModelJointLink){1,2};
    check(!sst_model_linked_pose(&m,source,2,links,1,j,3,p,n,4)&&zero(j,sizeof j));links[0].source=0;
    check(!sst_model_linked_pose(&m,source,2,links,1,j,3,p,n,4)&&zero(j,sizeof j));
    check(!sst_model_linked_pose(&m,NULL,0,links,1,j,3,p,n,4)&&zero(p,sizeof p));
    float saved[2][12];memcpy(saved,source,sizeof source);check(!sst_model_linked_pose(&m,source,2,links,1,source,2,p,n,4)&&!memcmp(saved,source,sizeof source));
    check(sst_model_linked_pose(&m,NULL,0,NULL,0,j,3,p,n,4));near(j[1][3],12);
}
static void rejected(void){SstMdl0 m;memset(&m,0x7f,sizeof m);check(!sst_mdl0_open(data,sizeof data,M,&m)&&zero(&m,sizeof m));}
static void malformed(void){
    fixture();w32(data,M+8,10);rejected();fixture();w32(data,M+4,UINT32_MAX);rejected();fixture();w32(data,INFO+28,5);rejected();
    fixture();w32(data,TABLE,1025);rejected();fixture();w32(data,TABLE,0);rejected();fixture();data[INFO+35]=3;rejected();fixture();data[INFO+35]=2;rejected();
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
            ok=sst_model_layers(&m,NULL,0,w,SST_CHR0_MAX_JOINTS,p,n,SST_MDL0_MAX_MATRICES);
            if(!ok)check(zero(w,sizeof w)&&zero(p,sizeof p)&&zero(n,sizeof n));
            else for(unsigned i=0;i<m.nodes;i++)for(unsigned k=0;k<12;k++)check(isfinite(w[i][k]));
            ok=sst_model_linked_pose(&m,NULL,0,NULL,0,w,SST_CHR0_MAX_JOINTS,p,n,SST_MDL0_MAX_MATRICES);
            if(!ok)check(zero(w,sizeof w)&&zero(p,sizeof p)&&zero(n,sizeof n));
            else for(unsigned i=0;i<m.nodes;i++)for(unsigned k=0;k<12;k++)check(isfinite(w[i][k]));
        }else check(zero(&m,sizeof m));check(!memcmp(saved,input,size));free(input);
    }
}
int main(void){numeric_palettes();binding_pose();layers_numeric();layers_patch_defaults();layers_rejection_and_aliases();linked_joint_callback();malformed();output_admission();corrupted();printf("resource model: %u checks PASS; weighted palettes, six masked quaternion layers, post-blend defaults, joint callbacks, NORMAL/APPROX normals, aliases, bounds and1024corrupted spans; no live rendering acceptance\n",checks);return 0;}

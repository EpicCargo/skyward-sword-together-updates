// Independent analytic world-space callback/chain checks. No game resources.
#include "../src/sst_resource_world.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
static const float id[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static const float identity[9]={1,0,0,0,1,0,0,0,1};
static const float z90[9]={0,-1,0,1,0,0,0,0,1},x90[9]={1,0,0,0,0,-1,0,1,0},y90[9]={0,0,1,0,1,0,-1,0,0};
static void check(bool ok){checks++;assert(ok);}
static void near(float a,float b){check(isfinite(a)&&fabsf(a-b)<.00001f);}
static bool zero(const void *p,size_t bytes){const unsigned char *s=p;for(size_t i=0;i<bytes;i++)if(s[i])return false;return true;}
static void initialize(float (*j)[12],unsigned n){for(unsigned i=0;i<n;i++)memcpy(j[i],id,sizeof id);}
static SstResourceWorldOp operation(unsigned joint,unsigned anchor){SstResourceWorldOp op={.joint=(uint16_t)joint,.anchor=(uint16_t)anchor};memcpy(op.rotation,identity,sizeof identity);return op;}
static void numeric(void){
    float in[4][12],saved[4][12],out[5][12];initialize(in,4);in[0][3]=10;in[0][7]=20;in[1][3]=12;in[1][7]=20;in[2][3]=14;in[2][7]=20;in[3][3]=99;
    memcpy(saved,in,sizeof in);memset(out,1,sizeof out);
    check(sst_resource_world_ops(in,4,NULL,0,out,5));check(!memcmp(out,in,sizeof in)&&zero(out[4],sizeof out[4]));
    SstResourceWorldOp ops[3]={operation(0,SST_WORLD_KEEP_POSITION),operation(1,0),operation(2,1)};
    memcpy(ops[0].rotation,z90,sizeof z90);ops[1].local_offset[0]=3;ops[2].local_offset[0]=4;
    SstResourceWorldOp saved_ops[3];memcpy(saved_ops,ops,sizeof ops);
    check(sst_resource_world_ops(in,4,ops,3,out,5));
    // Rotating a root preserves its position. The first anchor reads that
    // updated rotation; the next anchor reads the updated child position.
    near(out[0][3],10);near(out[0][7],20);near(out[0][0],0);near(out[0][4],1);
    near(out[1][3],10);near(out[1][7],23);near(out[1][0],1);
    near(out[2][3],14);near(out[2][7],23);check(!memcmp(out[3],in[3],sizeof id));
    check(!memcmp(in,saved,sizeof in)&&!memcmp(ops,saved_ops,sizeof ops));
    // No implicit hierarchy propagation: a rotation alone leaves all other
    // worlds byte-identical, even when they used to be descendants.
    check(sst_resource_world_ops(in,4,ops,1,out,5));check(!memcmp(out[1],in[1],3*sizeof id));
    // Pre-rotation is world-space multiplication, with observable order.
    memcpy(in[0],id,sizeof id);for(unsigned r=0;r<3;r++)for(unsigned c=0;c<3;c++)in[0][4*r+c]=y90[3*r+c];
    ops[0]=operation(0,SST_WORLD_KEEP_POSITION);memcpy(ops[0].rotation,x90,sizeof x90);
    check(sst_resource_world_ops(in,1,ops,1,out,5));
    const float xy[9]={0,0,1,1,0,0,0,1,0};for(unsigned r=0;r<3;r++)for(unsigned c=0;c<3;c++)near(out[0][4*r+c],xy[3*r+c]);
    // Self-anchor reads the old target before committing its new position;
    // world offsets are added after rotating a local anchor offset.
    initialize(in,4);in[0][3]=10;for(unsigned r=0;r<3;r++)for(unsigned c=0;c<3;c++)in[0][4*r+c]=z90[3*r+c];
    ops[0]=operation(0,0);ops[0].local_offset[0]=2;ops[0].world_offset[0]=1;ops[0].world_offset[1]=-3;
    check(sst_resource_world_ops(in,1,ops,1,out,5));near(out[0][3],11);near(out[0][7],-1);
    // Repeated targets execute in order. Two quarter-turns differ from one.
    initialize(in,4);ops[0]=operation(0,SST_WORLD_KEEP_POSITION);memcpy(ops[0].rotation,z90,sizeof z90);ops[1]=ops[0];
    ops[0].world_offset[0]=2;ops[1].world_offset[1]=3;
    check(sst_resource_world_ops(in,1,ops,2,out,5));near(out[0][0],-1);near(out[0][5],-1);near(out[0][3],2);near(out[0][7],3);
    // Translation may read a later-numbered joint. Node order is not FK order.
    ops[0]=operation(0,3);ops[0].local_offset[2]=7;in[3][3]=11;
    check(sst_resource_world_ops(in,4,ops,1,out,5));near(out[0][3],11);near(out[0][11],7);
}
static void limits(void){
    float in[256][12],out[256][12];SstResourceWorldOp ops[256];initialize(in,256);in[0][3]=10;
    for(unsigned i=0;i<256;i++){ops[i]=operation(i,i?i-1:0);ops[i].local_offset[0]=1;}
    check(sst_resource_world_ops(in,256,ops,256,out,256));for(unsigned i=0;i<256;i++){near(out[i][3],11+(float)i);near(out[i][0],1);near(out[i][7],0);}
    for(unsigned i=0;i<256;i++){ops[i]=operation(0,SST_WORLD_KEEP_POSITION);memcpy(ops[i].rotation,z90,sizeof z90);}
    check(sst_resource_world_ops(in,256,ops,256,out,256));check(!memcmp(out,in,sizeof in));
    // Tolerated native approximations are accepted, but repeated numerical
    // drift is rejected before publishing a palette-incompatible result.
    ops[0]=operation(0,SST_WORLD_KEEP_POSITION);ops[0].rotation[0]=1.0002f;
    check(sst_resource_world_ops(in,256,ops,1,out,256));
    for(unsigned i=1;i<256;i++)ops[i]=ops[0];memset(out,1,sizeof out);
    check(!sst_resource_world_ops(in,256,ops,256,out,256)&&zero(out,sizeof out));
}
static void invalid(void){
    float in[2][12],out[3][12],before[3][12];initialize(in,2);memset(out,1,sizeof out);memcpy(before,out,sizeof out);SstResourceWorldOp op=operation(0,SST_WORLD_KEEP_POSITION);
    // Invalid extents/overlap reject without output writes.
    check(!sst_resource_world_ops(in,0,&op,1,out,3));check(!sst_resource_world_ops(in,3,&op,1,out,2));check(!sst_resource_world_ops(in,257,&op,1,out,3));
    check(!sst_resource_world_ops(in,2,&op,257,out,3));check(!sst_resource_world_ops(in,2,&op,1,out,257));check(!sst_resource_world_ops(NULL,2,&op,1,out,3));
    check(!sst_resource_world_ops(in,2,NULL,1,out,3));check(!sst_resource_world_ops(in,2,&op,1,NULL,3));
    check(!sst_resource_world_ops((const float(*)[12])(uintptr_t)(UINTPTR_MAX-4),2,&op,1,out,3));
    check(!sst_resource_world_ops(in,2,(const SstResourceWorldOp*)(uintptr_t)(UINTPTR_MAX-4),1,out,3));
    check(!memcmp(out,before,sizeof out));
    float original[2][12];memcpy(original,in,sizeof in);check(!sst_resource_world_ops(in,2,&op,1,in,2)&&!memcmp(in,original,sizeof in));
    check(!sst_resource_world_ops(in,1,&op,1,(float(*)[12])((float*)in+1),2)&&!memcmp(in,original,sizeof in));
    check(!sst_resource_world_ops(in,2,(SstResourceWorldOp*)in,1,out,3)&&!memcmp(out,before,sizeof out));
    SstResourceWorldOp saved=op;check(!sst_resource_world_ops(in,1,&op,1,(float(*)[12])&op,1)&&!memcmp(&op,&saved,sizeof op));
    for(unsigned mode=0;mode<14;mode++){
        initialize(in,2);op=operation(0,SST_WORLD_KEEP_POSITION);memset(out,1,sizeof out);
        switch(mode){case 0:in[0][3]=NAN;break;case 1:in[0][0]=INFINITY;break;case 2:in[0][0]=-1;break;case 3:in[0][0]=2;break;
        case 4:op.joint=2;break;case 5:op.anchor=2;break;case 6:op.rotation[0]=NAN;break;case 7:op.rotation[0]=-1;break;
        case 8:op.local_offset[0]=1;break;case 9:op.world_offset[0]=INFINITY;break;case 10:op.world_offset[0]=1000001;break;
        case 11:in[0][3]=1000000;op.world_offset[0]=1;break;case 12:op.anchor=1;op.local_offset[0]=1000000;in[1][3]=1;break;
        case 13:op.anchor=1;op.local_offset[0]=NAN;break;}
        check(!sst_resource_world_ops(in,2,&op,1,out,3)&&zero(out,sizeof out));
    }
    // Late failure never leaks an earlier valid update.
    initialize(in,2);SstResourceWorldOp ops[2]={operation(0,SST_WORLD_KEEP_POSITION),operation(1,0)};ops[0].world_offset[0]=1000000;ops[1].local_offset[0]=1;
    check(!sst_resource_world_ops(in,2,ops,2,out,3)&&zero(out,sizeof out));
}
static void deterministic(void){
    float in[2][12],out[3][12];initialize(in,2);uint32_t seed=UINT32_C(0x57c0ffee);
    for(unsigned i=0;i<1024;i++){
        SstResourceWorldOp op=operation(1,0);op.local_offset[0]=3;SstResourceWorldOp original;
        seed=seed*1664525u+1013904223u;unsigned slot=(seed>>16)%15;seed=seed*1664525u+1013904223u;float mutation;memcpy(&mutation,&seed,4);
        if(slot<9)op.rotation[slot]=mutation;else if(slot<12)op.local_offset[slot-9]=mutation;else op.world_offset[slot-12]=mutation;
        original=op;memset(out,1,sizeof out);bool ok=sst_resource_world_ops(in,2,&op,1,out,3);
        check(!memcmp(&op,&original,sizeof op));if(ok){for(unsigned n=0;n<2;n++)for(unsigned k=0;k<12;k++)check(isfinite(out[n][k])&&fabsf(out[n][k])<=1e6f);check(zero(out[2],sizeof out[2]));}
        else check(zero(out,sizeof out));
    }
}
int main(void){numeric();limits();invalid();deterministic();printf("resource world: %u checks PASS; ordered world rotations, anchor chains, no descendant propagation, immutable inputs and atomic failure; no game\n",checks);return 0;}

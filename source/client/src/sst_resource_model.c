// SPDX-License-Identifier: MIT
#include "sst_resource_model.h"
#include <math.h>
#include <string.h>
typedef struct {const uint8_t *p;size_t bytes,lo,hi;bool ok;} R;
static bool span(R *r,size_t at,size_t n){if(!r->ok || at<r->lo || at>r->hi || n>r->hi-at){r->ok=false;return false;}return true;}
static unsigned r8(R *r,size_t at){return span(r,at,1)?r->p[at]:0;}
static unsigned r16(R *r,size_t at){return span(r,at,2)?((unsigned)r->p[at]<<8)|r->p[at+1]:0;}
static uint32_t r32(R *r,size_t at){return span(r,at,4)?(uint32_t)r->p[at]<<24|(uint32_t)r->p[at+1]<<16|(uint32_t)r->p[at+2]<<8|r->p[at+3]:0;}
static float rf(R *r,size_t at){uint32_t b=r32(r,at);float f;memcpy(&f,&b,4);if(!isfinite(f)||fabsf(f)>1e6f)r->ok=false;return f;}
static size_t rel(R *r,size_t base,uint32_t value){int64_t d=value<=INT32_MAX?(int64_t)value:(int64_t)value-INT64_C(4294967296);int64_t a=(int64_t)base+d;if(a<0 || (uint64_t)a>r->bytes){r->ok=false;return 0;}return(size_t)a;}
static bool text(R *r,size_t base,uint32_t value,char out[128]){
    size_t at=rel(r,base,value);out[0]=0;if(!value || !r->ok)return false;
    for(unsigned i=0;i<128;i++){if(at+i>=r->bytes)return false;unsigned c=r->p[at+i];if(!c)return i>0;if(c<32||c>126||i==127)return false;out[i]=(char)c;out[i+1]=0;}return false;
}
static R reader(const SstMdl0 *v){
    R r={0};if(!v||!v->data||v->bytes>SST_CHR0_MAX_BYTES||v->offset>v->end||v->end>v->bytes||v->end-v->offset<0x90||
        !v->nodes||v->nodes>SST_CHR0_MAX_JOINTS||!v->world_matrices||v->world_matrices>SST_MDL0_MAX_MATRICES||
        !v->view_matrices||v->view_matrices>v->world_matrices||v->envelope_mode>1)return r;
    r=(R){v->data,v->bytes,v->offset,v->end,true};if(r32(&r,v->offset)!=UINT32_C(0x4d444c30)||r32(&r,v->offset+8)!=11||r8(&r,v->offset+0x4c+35)!=v->envelope_mode)r.ok=false;
    span(&r,v->matrix_table,4+(size_t)v->world_matrices*4);span(&r,v->bones,24+(size_t)v->nodes*16);return r;
}
static bool dictionary(R *r,size_t at,unsigned maximum,unsigned *count){
    uint32_t size=r32(r,at),n=r32(r,at+4);if(!r->ok||n>maximum||size<24+(size_t)n*16||!span(r,at,size))return false;*count=n;return true;
}
static bool entry(R *r,size_t dic,unsigned i,size_t *at,char name[128]){
    if(!span(r,dic,24))return false;unsigned n=r32(r,dic+4);if(i>=n||!span(r,dic,24+(size_t)n*16))return false;
    size_t e=dic+24+(size_t)i*16;uint32_t no=r32(r,e+8),data=r32(r,e+12);*at=rel(r,dic,data);
    return r->ok&&data&&span(r,*at,1)&&text(r,dic,no,name);
}
static int map(R *r,const SstMdl0 *v,unsigned id){if(id>=v->world_matrices){r->ok=false;return -2;}uint32_t u=r32(r,v->matrix_table+4+4*(size_t)id);if(u==UINT32_MAX)return -1;if(u>=v->nodes){r->ok=false;return -2;}return(int)u;}
static bool node(const SstMdl0 *v,unsigned i,SstModelNode *out){
    R r=reader(v);size_t at;char name[128],inner[128];if(!r.ok||i>=v->nodes||!entry(&r,v->bones,i,&at,name)||!span(&r,at,0xd0))return false;
    uint32_t size=r32(&r,at);if(size<0xd0||!span(&r,at,size)||rel(&r,at,r32(&r,at+4))!=v->offset||
       !text(&r,at,r32(&r,at+8),inner)||strcmp(name,inner)||r32(&r,at+12)!=i||r32(&r,at+24)!=0)return false;
    unsigned matrix=r32(&r,at+16);if(matrix>=v->world_matrices||map(&r,v,matrix)!=(int)i)return false;
    SstModelNode n={0};memcpy(n.name,name,strlen(name)+1);n.parent=-1;n.matrix=(uint16_t)matrix;
    uint32_t parent=r32(&r,at+0x5c);
    if(parent){size_t p=rel(&r,at,parent);bool found=false;for(unsigned j=0;j<i;j++){size_t a;char ignored[128];if(!entry(&r,v->bones,j,&a,ignored))return false;if(a==p){n.parent=(int16_t)j;found=true;break;}}if(!found)return false;}
    for(unsigned a=0;a<3;a++){n.bind.scale[a]=rf(&r,at+0x20+4*a);n.bind.rotation[a]=rf(&r,at+0x2c+4*a);n.bind.translation[a]=rf(&r,at+0x38+4*a);if(n.bind.scale[a]!=1)return false;}
    for(unsigned k=0;k<12;k++){n.forward_bind[k]=rf(&r,at+0x70+4*k);n.inverse_bind[k]=rf(&r,at+0xa0+4*k);}
    if(!r.ok)return false;*out=n;return true;
}
static bool multiply(const float a[12],const float b[12],float out[12]){
    float t[12];for(unsigned row=0;row<3;row++)for(unsigned col=0;col<4;col++){double x=col==3?a[4*row+3]:0;for(unsigned k=0;k<3;k++)x+=(double)a[4*row+k]*b[4*k+col];if(!isfinite(x)||fabs(x)>1e6)return false;t[4*row+col]=(float)x;}memcpy(out,t,sizeof t);return true;
}
static bool rigid(const float a[12]){
    for(unsigned k=0;k<12;k++)if(!isfinite(a[k])||fabsf(a[k])>1e6f)return false;
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<=i;j++){double d=0;for(unsigned k=0;k<3;k++)d+=(double)a[4*i+k]*a[4*j+k];if(fabs(d-(i==j?1:0))>.005)return false;}
    double det=a[0]*((double)a[5]*a[10]-(double)a[6]*a[9])-a[1]*((double)a[4]*a[10]-(double)a[6]*a[8])+a[2]*((double)a[4]*a[9]-(double)a[5]*a[8]);return fabs(det-1)<.01;
}
static const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
// One checked interpreter serves admission and execution. The dry pass performs
// identical ID/readiness/weight validation without doing matrix arithmetic.
static bool program(const SstMdl0 *v,const float (*joints)[12],float (*slots)[12]){
    R r=reader(v);if(!r.ok||((joints==NULL)!=(slots==NULL))||v->tree<v->offset||v->tree>v->tree_end||v->tree_end>v->end||v->tree_end-v->tree>SST_MDL0_MAX_PROGRAM_BYTES)return false;
    int16_t owners[SST_MDL0_MAX_MATRICES];bool seen[SST_CHR0_MAX_JOINTS]={0},enveloped[SST_MDL0_MAX_MATRICES]={0};
    float envelopes[SST_MDL0_MAX_MATRICES][12];for(unsigned i=0;i<v->world_matrices;i++)owners[i]=-2;owners[0]=-1;if(slots)memcpy(slots[0],identity,sizeof identity);
    size_t at=v->tree;r.lo=at;r.hi=v->tree_end;bool ended=false;
    for(unsigned step=0;step<SST_MDL0_MAX_OPS;step++){
        unsigned op=r8(&r,at++);if(!r.ok)return false;if(op==1){ended=true;break;}
        unsigned a=r16(&r,at),b=r16(&r,at+2);at+=4;if(!r.ok||a>=SST_MDL0_MAX_MATRICES||b>=v->world_matrices)return false;
        if(op==2){
            SstModelNode n;if(a>=v->nodes||seen[a]||!node(v,a,&n)||owners[b]!=n.parent||
                (owners[n.matrix]!=-2&&!(n.matrix==0&&owners[0]==-1)))return false;
            seen[a]=true;owners[n.matrix]=(int16_t)a;if(slots)memcpy(slots[n.matrix],joints[a],sizeof slots[0]);
        }else if(op==6){
            if(a>=v->world_matrices||a==b||owners[a]!=-2||owners[b]==-2)return false;
            owners[a]=owners[b];if(slots)memcpy(slots[a],slots[b],sizeof slots[0]);
        }else return false;
    }
    if(!ended)return false;for(unsigned i=0;i<v->nodes;i++)if(!seen[i])return false;
    r=reader(v);
    if(v->mix){
        if(v->mix<v->offset||v->mix>v->mix_end||v->mix_end>v->end||v->mix_end-v->mix>SST_MDL0_MAX_PROGRAM_BYTES)return false;
        at=v->mix;r.lo=at;r.hi=v->mix_end;ended=false;
        for(unsigned step=0;step<SST_MDL0_MAX_OPS;step++){
            unsigned op=r8(&r,at++);if(!r.ok)return false;if(op==1){ended=true;break;}
            unsigned dst=r16(&r,at);at+=2;if(!r.ok||dst>=v->world_matrices)return false;
            if(op==5){
                unsigned id=r16(&r,at);at+=2;SstModelNode n;
                if(!r.ok||id>=v->nodes||owners[dst]!=(int)id||enveloped[dst]||!node(v,id,&n))return false;
                enveloped[dst]=true;if(slots&&!multiply(slots[dst],n.inverse_bind,envelopes[dst]))return false;
            }else if(op==3){
                unsigned count=r8(&r,at++);if(!r.ok||!count||count>SST_MDL0_MAX_INFLUENCES||owners[dst]!=-2)return false;
                R metadata=reader(v);if(map(&metadata,v,dst)!=-1||!metadata.ok)return false;
                unsigned sources[SST_MDL0_MAX_INFLUENCES];double weight_sum=0;float result[12]={0};
                for(unsigned i=0;i<count;i++){
                    unsigned src=r16(&r,at);float w=rf(&r,at+2);at+=6;
                    if(!r.ok||src>=v->world_matrices||!enveloped[src]||w<0||w>1)return false;
                    for(unsigned j=0;j<i;j++)if(sources[j]==src)return false;sources[i]=src;weight_sum+=w;
                    if(slots)for(unsigned k=0;k<12;k++){result[k]=fmaf(w,envelopes[src][k],result[k]);if(!isfinite(result[k])||fabsf(result[k])>1e6f)return false;}
                }
                if(fabs(weight_sum-1)>1e-5)return false;owners[dst]=-3;if(slots)memcpy(slots[dst],result,sizeof result);
            }else return false;
        }
        if(!ended)return false;
    }
    r=reader(v);for(unsigned i=0;i<v->world_matrices;i++){int mapped=map(&r,v,i);if(!r.ok||owners[i]!=(mapped<0?-3:mapped))return false;}return true;
}
bool sst_mdl0_open(const void *data,size_t bytes,size_t offset,SstMdl0 *out){
    if(!out)return false;*out=(SstMdl0){0};if(!data||bytes>SST_CHR0_MAX_BYTES||offset>bytes||bytes-offset<0x90)return false;
    R r={data,bytes,offset,bytes,true};if(r32(&r,offset)!=UINT32_C(0x4d444c30)||r32(&r,offset+8)!=11)return false;
    uint32_t size=r32(&r,offset+4);if(size<0x90||size>bytes-offset)return false;r.hi=offset+size;
    SstMdl0 v={.data=data,.bytes=bytes,.offset=offset,.end=r.hi};size_t info=offset+0x4c;
    if(r32(&r,info)<0x40||rel(&r,info,r32(&r,info+4))!=offset||r8(&r,info+35)>1||
        r8(&r,info+32)>1||r8(&r,info+33)>1||r8(&r,info+34)>1)return false;
    v.envelope_mode=(uint8_t)r8(&r,info+35);v.scaling_rule=r32(&r,info+8);unsigned views=r32(&r,info+28);v.matrix_table=rel(&r,info,r32(&r,info+36));unsigned worlds=r32(&r,v.matrix_table);
    if(!r.ok||v.scaling_rule>2||!worlds||worlds>SST_MDL0_MAX_MATRICES||!views||views>worlds)return false;
    v.world_matrices=(uint16_t)worlds;v.view_matrices=(uint16_t)views;v.bones=rel(&r,offset,r32(&r,offset+20));unsigned count;
    if(!dictionary(&r,v.bones,SST_CHR0_MAX_JOINTS,&count)||!count||!text(&r,offset,r32(&r,offset+0x48),v.name))return false;v.nodes=(uint16_t)count;
    size_t graph=rel(&r,offset,r32(&r,offset+16));unsigned programs;
    if(!dictionary(&r,graph,16,&programs))return false;size_t starts[16];char names[16][128];
    for(unsigned i=0;i<programs;i++){
        if(!entry(&r,graph,i,&starts[i],names[i]))return false;
        for(unsigned j=0;j<i;j++)if(starts[i]==starts[j]||!strcmp(names[i],names[j]))return false;
        if(!strcmp(names[i],"NodeTree"))v.tree=starts[i];if(!strcmp(names[i],"NodeMix"))v.mix=starts[i];
    }
    if(!v.tree)return false;v.tree_end=v.end;v.mix_end=v.mix?v.end:0;
    for(unsigned i=0;i<programs;i++){if(starts[i]>v.tree&&starts[i]<v.tree_end)v.tree_end=starts[i];if(v.mix&&starts[i]>v.mix&&starts[i]<v.mix_end)v.mix_end=starts[i];}
    SstResourceSrt bind[SST_CHR0_MAX_JOINTS];int16_t parents[SST_CHR0_MAX_JOINTS];float world[SST_CHR0_MAX_JOINTS][12];char node_names[SST_CHR0_MAX_JOINTS][128];
    for(unsigned i=0;i<v.nodes;i++){
        SstModelNode n;if(!node(&v,i,&n)||!rigid(n.forward_bind)||!rigid(n.inverse_bind))return false;
        memcpy(node_names[i],n.name,strlen(n.name)+1);for(unsigned j=0;j<i;j++)if(!strcmp(node_names[i],node_names[j]))return false;
        float product[12];if(!multiply(n.forward_bind,n.inverse_bind,product))return false;for(unsigned k=0;k<12;k++)if(fabsf(product[k]-identity[k])>.01f)return false;
        bind[i]=n.bind;parents[i]=n.parent;
    }
    if(!sst_resource_pose_world(parents,bind,v.nodes,world,v.nodes))return false;
    for(unsigned i=0;i<v.nodes;i++){SstModelNode n;if(!node(&v,i,&n))return false;for(unsigned k=0;k<12;k++)if(fabsf(world[i][k]-n.forward_bind[k])>.01f)return false;}
    if(!program(&v,NULL,NULL))return false;*out=v;return true;
}
bool sst_mdl0_node(const SstMdl0 *v,unsigned i,SstModelNode *out){if(!out)return false;*out=(SstModelNode){0};return node(v,i,out);}
static bool overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;if(an>UINTPTR_MAX-x||bn>UINTPTR_MAX-y)return true;return x<y+bn&&y<x+an;}
static bool outputs(const SstMdl0 *v,float (*p)[12],float (*n)[12],unsigned cap){
    if(!p||!n||!cap||cap>SST_MDL0_MAX_MATRICES)return false;size_t size=(size_t)cap*sizeof *p;
    return !overlap(p,size,n,size)&&(!v||(!overlap(p,size,v,sizeof *v)&&!overlap(n,size,v,sizeof *v)&&!overlap(p,size,v->data,v->bytes)&&!overlap(n,size,v->data,v->bytes)));
}
bool sst_mdl0_palettes(const SstMdl0 *v,const float (*joints)[12],unsigned count,float (*p)[12],float (*n)[12],unsigned cap){
    if(!outputs(v,p,n,cap)||count>SST_CHR0_MAX_JOINTS||(joints&&(overlap(p,(size_t)cap*sizeof *p,joints,(size_t)count*sizeof *joints)||overlap(n,(size_t)cap*sizeof *n,joints,(size_t)count*sizeof *joints))))return false;
    memset(p,0,(size_t)cap*sizeof *p);memset(n,0,(size_t)cap*sizeof *n);R r=reader(v);if(!r.ok||!joints||count!=v->nodes||cap<v->view_matrices)return false;
    for(unsigned i=0;i<count;i++)if(!rigid(joints[i]))return false;
    float slots[SST_MDL0_MAX_MATRICES][12];if(!program(v,joints,slots))return false;
    for(unsigned i=0;i<v->view_matrices;i++)for(unsigned k=0;k<12;k++){p[i][k]=slots[i][k];n[i][k]=k%4==3?0:slots[i][k];}return true;
}
bool sst_model_bind(const SstMdl0 *m,const SstChr0 *a,SstModelBinding *out){
    if(!out)return false;*out=(SstModelBinding){0};R r=reader(m);if(!r.ok||!a||!a->data||!a->joints||a->joints>SST_CHR0_MAX_JOINTS)return false;
    SstModelBinding b={.model=*m,.animation=*a};for(unsigned i=0;i<m->nodes;i++){
        SstModelNode n;if(!node(m,i,&n))return false;b.tracks[i]=-1;
        for(unsigned j=0;j<a->joints;j++){char name[128];if(!sst_chr0_joint_name(a,j,name,sizeof name))return false;if(!strcmp(n.name,name)){b.tracks[i]=(int16_t)j;b.matched++;break;}}
    }*out=b;return true;
}
bool sst_model_pose(const SstModelBinding *b,float frame,float (*j)[12],unsigned jc,float (*p)[12],float (*n)[12],unsigned pc){
    if(!b||!j||!jc||jc>SST_CHR0_MAX_JOINTS||!outputs(&b->model,p,n,pc))return false;
    size_t jb=(size_t)jc*sizeof *j,pb=(size_t)pc*sizeof *p;
    if(overlap(j,jb,b,sizeof *b)||overlap(p,pb,b,sizeof *b)||overlap(n,pb,b,sizeof *b)||overlap(j,jb,p,pb)||overlap(j,jb,n,pb)||overlap(j,jb,b->model.data,b->model.bytes)||overlap(j,jb,b->animation.data,b->animation.bytes)||
       overlap(p,pb,b->animation.data,b->animation.bytes)||overlap(n,pb,b->animation.data,b->animation.bytes))return false;
    memset(j,0,jb);memset(p,0,pb);memset(n,0,pb);R r=reader(&b->model);if(!r.ok||jc<b->model.nodes||!isfinite(frame)||frame<0||frame>b->animation.duration)return false;
    SstResourceSrt local[SST_CHR0_MAX_JOINTS];int16_t parents[SST_CHR0_MAX_JOINTS];float world[SST_CHR0_MAX_JOINTS][12];
    for(unsigned i=0;i<b->model.nodes;i++){SstModelNode node_data;if(!node(&b->model,i,&node_data))return false;parents[i]=node_data.parent;
        if(b->tracks[i]< -1)return false;if(b->tracks[i]<0)local[i]=node_data.bind;
        else {char name[128];if(!sst_chr0_joint_name(&b->animation,(unsigned)b->tracks[i],name,sizeof name)||strcmp(name,node_data.name)||
            !sst_chr0_sample(&b->animation,(unsigned)b->tracks[i],frame,&node_data.bind,&local[i]))return false;}
    }
    if(!sst_resource_pose_world(parents,local,b->model.nodes,world,b->model.nodes)||!sst_mdl0_palettes(&b->model,world,b->model.nodes,p,n,pc))return false;
    memcpy(j,world,(size_t)b->model.nodes*sizeof *j);return true;
}

// Native blend order/patch semantics are documented in docs/resource-layers.md.
// Original bounded arithmetic; no native animation objects or pointers retained.
static bool joint_outputs(const SstMdl0 *m,float (*j)[12],unsigned jc,float (*p)[12],float (*n)[12],unsigned pc){
    if(!m||!j||!jc||jc>SST_CHR0_MAX_JOINTS||!outputs(m,p,n,pc))return false;
    size_t jb=(size_t)jc*sizeof *j,pb=(size_t)pc*sizeof *p;
    return !overlap(j,jb,m,sizeof *m)&&!overlap(j,jb,p,pb)&&!overlap(j,jb,n,pb)&&!overlap(j,jb,m->data,m->bytes);
}
static bool writes_overlap(const void *input,size_t bytes,float (*j)[12],unsigned jc,float (*p)[12],float (*n)[12],unsigned pc){
    return input&&bytes&&(overlap(input,bytes,j,(size_t)jc*sizeof *j)||overlap(input,bytes,p,(size_t)pc*sizeof *p)||overlap(input,bytes,n,(size_t)pc*sizeof *n));
}
static void clear_pose(float (*j)[12],unsigned jc,float (*p)[12],float (*n)[12],unsigned pc){
    memset(j,0,(size_t)jc*sizeof *j);memset(p,0,(size_t)pc*sizeof *p);memset(n,0,(size_t)pc*sizeof *n);
}
static bool same_model(const SstMdl0 *a,const SstMdl0 *b){
    return a->data==b->data&&a->bytes==b->bytes&&a->offset==b->offset&&a->end==b->end&&a->bones==b->bones&&
        a->matrix_table==b->matrix_table&&a->tree==b->tree&&a->tree_end==b->tree_end&&a->mix==b->mix&&a->mix_end==b->mix_end&&
        a->nodes==b->nodes&&a->world_matrices==b->world_matrices&&a->view_matrices==b->view_matrices&&
        a->scaling_rule==b->scaling_rule&&a->envelope_mode==b->envelope_mode;
}
static bool local_matrix(const SstResourceSrt *s,float out[12]){
    const int16_t root=-1;return sst_resource_pose_world(&root,s,1,(float(*)[12])out,1);
}
static bool compose(const SstMdl0 *m,const float (*local)[12],float (*world)[12]){
    for(unsigned i=0;i<m->nodes;i++){
        SstModelNode node_data;if(!node(m,i,&node_data)||!rigid(local[i]))return false;
        if(node_data.parent<0)memcpy(world[i],local[i],sizeof world[i]);
        else if(!multiply(world[node_data.parent],local[i],world[i]))return false;
    }return true;
}
static bool to_quaternion(const float m[12],float q[4]){
    float trace=m[0]+m[5]+m[10];
    if(trace>0){
        float root=sqrtf(1+trace),r=.5f/root;q[3]=.5f*root;
        q[0]=(m[9]-m[6])*r;q[1]=(m[2]-m[8])*r;q[2]=(m[4]-m[1])*r;
    }else{
        unsigned a=0;if(m[5]>m[0])a=1;if(m[10]>m[5*a])a=2;unsigned b=(a+1)%3,c=(a+2)%3;
        float root=sqrtf(1+m[5*a]-m[5*b]-m[5*c]);if(!(root>0))return false;
        float r=.5f/root;q[a]=.5f*root;q[b]=(m[4*a+b]+m[4*b+a])*r;
        q[c]=(m[4*a+c]+m[4*c+a])*r;q[3]=(m[4*c+b]-m[4*b+c])*r;
    }
    for(unsigned k=0;k<4;k++)if(!isfinite(q[k]))return false;return true;
}
static bool slerp(float state[4],const float target[4],float t){
    float dot=0;for(unsigned k=0;k<4;k++)dot+=state[k]*target[k];float sign=dot<0?-1.f:1.f;dot=fabsf(dot);
    float a=1-t,b=t;
    if(dot<=1.f-1e-5f){float angle=acosf(dot),s=sinf(angle);if(!(s>0))return false;a=sinf((1-t)*angle)/s;b=sinf(t*angle)/s;}
    for(unsigned k=0;k<4;k++){state[k]=a*state[k]+sign*b*target[k];if(!isfinite(state[k]))return false;}return true;
}
static bool from_quaternion(const float q[4],float out[12]){
    float norm=0;for(unsigned k=0;k<4;k++)norm+=q[k]*q[k];if(!isfinite(norm)||norm<.25f||norm>4)return false;
    float factor=2/norm,x=q[0],y=q[1],z=q[2],w=q[3];
    float xx=x*x*factor,yy=y*y*factor,zz=z*z*factor,xy=x*y*factor,xz=x*z*factor,yz=y*z*factor;
    float wx=w*x*factor,wy=w*y*factor,wz=w*z*factor;
    float m[12]={1-yy-zz,xy-wz,xz+wy,0,xy+wz,1-xx-zz,yz-wx,0,xz-wy,yz+wx,1-xx-yy,0};
    memcpy(out,m,sizeof m);return rigid(out);
}
static bool layer_sample(const SstModelLayer *layer,unsigned index,const SstModelNode *node_data,const SstResourceSrt *defaults,SstResourceSrt *out){
    int track=layer->binding->tracks[index];char name[128];
    return track>=0&&sst_chr0_joint_name(&layer->binding->animation,(unsigned)track,name,sizeof name)&&!strcmp(name,node_data->name)&&
        sst_chr0_sample(&layer->binding->animation,(unsigned)track,layer->frame,defaults,out);
}
bool sst_model_layers(const SstMdl0 *m,const SstModelLayer *layers,unsigned count,float (*j)[12],unsigned jc,float (*p)[12],float (*n)[12],unsigned pc){
    if(count>SST_MODEL_MAX_LAYERS||!joint_outputs(m,j,jc,p,n,pc)||writes_overlap(layers,(size_t)count*sizeof *layers,j,jc,p,n,pc))return false;
    // Admit all input spans before clearing outputs, including zero-weight layers.
    if(layers)for(unsigned l=0;l<count;l++){
        const SstModelLayer *x=&layers[l];if(x->mask_count>SST_CHR0_MAX_JOINTS||writes_overlap(x->mask,x->mask_count,j,jc,p,n,pc))return false;
        if(x->binding&&(writes_overlap(x->binding,sizeof *x->binding,j,jc,p,n,pc)||
            writes_overlap(x->binding->model.data,x->binding->model.bytes,j,jc,p,n,pc)||
            writes_overlap(x->binding->animation.data,x->binding->animation.bytes,j,jc,p,n,pc)))return false;
    }
    clear_pose(j,jc,p,n,pc);R r=reader(m);if(!r.ok||jc<m->nodes||pc<m->view_matrices||(count&&!layers))return false;
    for(unsigned l=0;l<count;l++){
        const SstModelLayer *x=&layers[l];
        if(!isfinite(x->weight)||x->weight<0||x->weight>1e6f||(!x->mask&&x->mask_count)||(x->mask&&x->mask_count!=m->nodes))return false;
        if(x->mask)for(unsigned i=0;i<m->nodes;i++)if(x->mask[i]>1)return false;
        if(!x->binding){if(x->weight||x->mask)return false;continue;}
        if(!same_model(m,&x->binding->model))return false;
        if(x->weight&&(!isfinite(x->frame)||x->frame<0||x->frame>x->binding->animation.duration))return false;
        for(unsigned i=0;i<m->nodes;i++)if(x->binding->tracks[i]< -1)return false;
    }
    const SstResourceSrt identity_srt={{1,1,1},{0,0,0},{0,0,0},0};
    float local[SST_CHR0_MAX_JOINTS][12],world[SST_CHR0_MAX_JOINTS][12];
    for(unsigned i=0;i<m->nodes;i++){
        SstModelNode node_data;if(!node(m,i,&node_data))return false;
        unsigned active[SST_MODEL_MAX_LAYERS],num=0;float total=0;
        for(unsigned l=0;l<count;l++)if(layers[l].binding&&layers[l].weight&&(!layers[l].mask||layers[l].mask[i])&&layers[l].binding->tracks[i]>=0){active[num++]=l;total+=layers[l].weight;}
        if(!num){if(!local_matrix(&node_data.bind,local[i]))return false;continue;}
        if(num==1){SstResourceSrt s;if(!layer_sample(&layers[active[0]],i,&node_data,&node_data.bind,&s)||!local_matrix(&s,local[i]))return false;continue;}
        float q[4]={0,0,0,1},translation[3]={0},added=0;uint32_t common=UINT32_MAX;
        for(unsigned a=0;a<num;a++){
            const SstModelLayer *x=&layers[active[a]];SstResourceSrt s;float matrix[12],target[4];
            if(!layer_sample(x,i,&node_data,&identity_srt,&s)||!local_matrix(&s,matrix)||!to_quaternion(matrix,target))return false;
            common&=s.flags;added+=x->weight;if(!slerp(q,target,x->weight/added))return false;
            float ratio=x->weight/total;for(unsigned k=0;k<3;k++)translation[k]+=ratio*s.translation[k];
        }
        if(!from_quaternion(q,local[i]))return false;
        if(common&(1u<<8)){
            float bind[12];if(!local_matrix(&node_data.bind,bind))return false;
            for(unsigned k=0;k<12;k++)if(k%4!=3)local[i][k]=bind[k];
        }
        for(unsigned k=0;k<3;k++)local[i][4*k+3]=common&(1u<<9)?node_data.bind.translation[k]:translation[k];
    }
    if(!compose(m,local,world)||!sst_mdl0_palettes(m,world,m->nodes,p,n,pc))return false;
    memcpy(j,world,(size_t)m->nodes*sizeof *j);return true;
}
bool sst_model_linked_pose(const SstMdl0 *m,const float (*source)[12],unsigned sc,const SstModelJointLink *links,unsigned lc,float (*j)[12],unsigned jc,float (*p)[12],float (*n)[12],unsigned pc){
    if(sc>SST_CHR0_MAX_JOINTS||lc>SST_CHR0_MAX_JOINTS||!joint_outputs(m,j,jc,p,n,pc)||
        writes_overlap(source,(size_t)sc*sizeof *source,j,jc,p,n,pc)||writes_overlap(links,(size_t)lc*sizeof *links,j,jc,p,n,pc))return false;
    clear_pose(j,jc,p,n,pc);R r=reader(m);if(!r.ok||jc<m->nodes||pc<m->view_matrices||(lc&&(!source||!links||!sc)))return false;
    SstResourceSrt bind[SST_CHR0_MAX_JOINTS];int16_t parents[SST_CHR0_MAX_JOINTS];float world[SST_CHR0_MAX_JOINTS][12];bool seen[SST_CHR0_MAX_JOINTS]={0};
    for(unsigned i=0;i<m->nodes;i++){SstModelNode node_data;if(!node(m,i,&node_data))return false;bind[i]=node_data.bind;parents[i]=node_data.parent;}
    if(!sst_resource_pose_world(parents,bind,m->nodes,world,m->nodes))return false;
    for(unsigned i=0;i<lc;i++){
        unsigned dst=links[i].target,src=links[i].source;if(dst>=m->nodes||src>=sc||seen[dst]||!rigid(source[src]))return false;
        seen[dst]=true;memcpy(world[dst],source[src],sizeof world[dst]);
    }
    if(!sst_mdl0_palettes(m,world,m->nodes,p,n,pc))return false;
    memcpy(j,world,(size_t)m->nodes*sizeof *j);return true;
}

// SPDX-License-Identifier: MIT
#include "sst_resource_pose.h"
#include <math.h>
#include <string.h>
_Static_assert(sizeof(float)==4,"CHR0 requires IEEE binary32 storage");

// Format references and limits are documented in docs/resource-pose.md.
// All arithmetic/decoding below is independent of native pointers and structs.
typedef struct {const uint8_t *p;size_t bytes,lo,hi;bool ok;} Reader;
static bool range(Reader *r,size_t at,size_t count) {
    if(!r->ok || at<r->lo || at>r->hi || count>r->hi-at){r->ok=false;return false;}
    return true;
}
static uint32_t u32(Reader *r,size_t at) {
    if(!range(r,at,4))return 0;
    return (uint32_t)r->p[at]<<24|(uint32_t)r->p[at+1]<<16|(uint32_t)r->p[at+2]<<8|r->p[at+3];
}
static uint16_t u16(Reader *r,size_t at) {
    if(!range(r,at,2))return 0;
    return (uint16_t)((unsigned)r->p[at]<<8|r->p[at+1]);
}
static float f32(Reader *r,size_t at) {
    uint32_t bits=u32(r,at);float value;memcpy(&value,&bits,4);
    if(!isfinite(value) || fabsf(value)>1e6f)r->ok=false;
    return value;
}
static int signed16(uint16_t v){return v>=32768?(int)v-65536:(int)v;}
static size_t relative(Reader *r,size_t base,uint32_t encoded) {
    int64_t delta=encoded<=INT32_MAX?(int64_t)encoded:(int64_t)encoded-INT64_C(4294967296);
    int64_t result=(int64_t)base+delta;
    if(result<0 || (uint64_t)result>r->bytes){r->ok=false;return 0;}
    return (size_t)result;
}
static bool name(Reader *r,size_t base,uint32_t encoded,char out[SST_CHR0_NAME_BYTES]) {
    size_t at=relative(r,base,encoded);out[0]=0;
    if(!encoded || !r->ok)return false;
    for(unsigned i=0;i<SST_CHR0_NAME_BYTES;i++) {
        if(at+i>=r->bytes)return false;
        unsigned c=r->p[at+i];
        if(!c)return i>0;
        if(c<32 || c>126 || i==SST_CHR0_NAME_BYTES-1)return false;
        out[i]=(char)c;out[i+1]=0;
    }
    return false;
}
static Reader reader(const SstChr0 *v) {
    Reader r={0};
    if(!v || !v->data || !v->bytes || v->bytes>SST_CHR0_MAX_BYTES ||
       v->offset>v->end || v->end>v->bytes || v->end-v->offset<0x28 ||
       !v->joints || v->joints>SST_CHR0_MAX_JOINTS)return r;
    r=(Reader){v->data,v->bytes,v->offset,v->end,true};
    if(u32(&r,v->offset)!=UINT32_C(0x43485230))r.ok=false;
    if(!range(&r,v->dictionary,24+(size_t)v->joints*16))r.ok=false;
    return r;
}
static bool node(const SstChr0 *v,unsigned index,size_t *at,char out[SST_CHR0_NAME_BYTES]) {
    Reader r=reader(v);char from_node[SST_CHR0_NAME_BYTES];
    if(!r.ok || index>=v->joints)return false;
    size_t entry=v->dictionary+24+(size_t)index*16;
    uint32_t no=u32(&r,entry+8),data=u32(&r,entry+12);
    *at=relative(&r,v->dictionary,data);
    uint32_t inner=u32(&r,*at);
    return r.ok && name(&r,v->dictionary,no,out) && name(&r,*at,inner,from_node) && !strcmp(out,from_node);
}
typedef struct {double time,value,slope;} Key;
static bool key(Reader *r,size_t curve,unsigned format,unsigned index,Key *out) {
    size_t at=curve+(format==3?8:16)+(size_t)index*(format==1?4:format==2?6:12);
    if(format==1) {
        uint32_t packed=u32(r,at);int tangent=(int)(packed&4095u);
        if(tangent&2048)tangent-=4096;
        out->time=packed>>24;out->value=((packed>>12)&4095u)*(double)f32(r,curve+8)+f32(r,curve+12);
        out->slope=tangent/32.0;
    } else if(format==2) {
        out->time=signed16(u16(r,at))/32.0;
        out->value=u16(r,at+2)*(double)f32(r,curve+8)+f32(r,curve+12);
        out->slope=signed16(u16(r,at+4))/256.0;
    } else {
        out->time=f32(r,at);out->value=f32(r,at+4);out->slope=f32(r,at+8);
    }
    return r->ok && isfinite(out->value) && fabs(out->value)<=1e6 && fabs(out->time)<=65536;
}
static bool curve_value(Reader *r,size_t curve,unsigned format,unsigned duration,float frame,float *out) {
    double value=0;
    if(format>=4 && format<=6) {
        unsigned width=format==4?1:format==5?2:4;
        size_t table=curve+(format==6?0:8),count=(size_t)duration+1;
        if(count>SST_CHR0_MAX_KEYS || !range(r,table,count*width))return false;
        double scale=1,bias=0;
        if(format!=6){scale=f32(r,curve);bias=f32(r,curve+4);}
        // Validate every value, even when sampling an endpoint. No malformed
        // tail can be admitted because an earlier sample happened to be finite.
        double before=0,after=0;unsigned left=(unsigned)frame,right=left<duration?left+1:left;
        for(unsigned i=0;i<count;i++) {
            double v=format==6?f32(r,table+4*i):
                (format==5?u16(r,table+2*i):r->p[table+i])*scale+bias;
            if(!r->ok || !isfinite(v) || fabs(v)>1e6)return false;
            if(i==left)before=v;
            if(i==right)after=v;
        }
        value=before+((double)frame-left)*(after-before);
    } else if(format>=1 && format<=3) {
        unsigned count=u16(r,curve);float inverse_range=f32(r,curve+4);
        if(!r->ok || !count || count>SST_CHR0_MAX_KEYS || inverse_range<0)return false;
        size_t width=format==1?4:format==2?6:12,table=curve+(format==3?8:16);
        if(!range(r,table,(size_t)count*width))return false;
        Key first={0},last={0},before={0},after={0};bool have_before=false,have_after=false;
        for(unsigned i=0;i<count;i++) {
            Key current;
            if(!key(r,curve,format,i,&current) || (i && current.time<last.time))return false;
            if(!i)first=current;
            last=current;
            if(current.time<=frame){before=current;have_before=true;}
            else if(!have_after){after=current;have_after=true;}
        }
        // Packed resources can have coincident keys. Native sampling clamps to
        // the FIRST key at the initial endpoint, and selects the LAST key at an
        // interior coincident time. The first strictly later key bounds Hermite.
        if(frame<=first.time || !have_before)value=first.value;
        else if(frame>=last.time || !have_after)value=last.value;
        else {
            double length=after.time-before.time,t=((double)frame-before.time)/length;
            double t2=t*t,t3=t2*t;
            value=(2*t3-3*t2+1)*before.value+(t3-2*t2+t)*length*before.slope+
                (-2*t3+3*t2)*after.value+(t3-t2)*length*after.slope;
        }
    } else return false;
    if(!r->ok || !isfinite(value) || fabs(value)>1e6)return false;
    *out=(float)value;return true;
}
static bool component(Reader *r,size_t base,size_t *slot,unsigned format,bool constant,unsigned duration,float frame,float *out) {
    if(!range(r,*slot,4))return false;
    size_t at=*slot;*slot+=4;
    if(!format || constant){*out=f32(r,at);return r->ok;}
    uint32_t encoded=u32(r,at);size_t curve=relative(r,base,encoded);
    return encoded && r->ok && curve_value(r,curve,format,duration,frame,out);
}
static bool sample(const SstChr0 *v,unsigned index,float frame,const SstResourceSrt *bind,SstResourceSrt *out) {
    Reader r=reader(v);size_t at;char joint[SST_CHR0_NAME_BYTES];
    if(!r.ok || !isfinite(frame) || frame<0 || frame>v->duration || !node(v,index,&at,joint))return false;
    uint32_t flags=u32(&r,at+4);unsigned formats[3]={(flags>>25)&3u,(flags>>27)&7u,(flags>>30)&3u};
    if(!r.ok || !(flags&1u) || formats[1]>6)return false;
    SstResourceSrt result={{1,1,1},{0,0,0},{0,0,0},flags};
    float *fields[3]={result.scale,result.rotation,result.translation};
    const float *defaults[3]={bind?bind->scale:NULL,bind?bind->rotation:NULL,bind?bind->translation:NULL};
    unsigned model_bits[3]={7,8,9},zero_bits[3]={3,5,6},constant_bits[3]={13,16,19};
    size_t slot=at+8;
    for(unsigned group=0;group<3;group++) {
        bool identity=(flags&(1u<<1)) || (group && (flags&(1u<<2))) || (flags&(1u<<zero_bits[group]));
        bool model=!identity && (flags&(1u<<model_bits[group]));
        bool required=(flags&(1u<<(22+group)))!=0;
        if(required!=(!identity && !model))return false;
        if(identity)continue;
        if(model) {
            if(!bind)return false;
            for(unsigned axis=0;axis<3;axis++) {
                float value=defaults[group][axis];if(!isfinite(value) || fabsf(value)>1e6f)return false;
                fields[group][axis]=value;
            }
            continue;
        }
        unsigned axes=group==0 && (flags&(1u<<4))?1:3;
        for(unsigned axis=0;axis<axes;axis++)
            if(!component(&r,at,&slot,formats[group],(flags&(1u<<(constant_bits[group]+axis)))!=0,v->duration,frame,&fields[group][axis]))return false;
        if(axes==1)fields[group][1]=fields[group][2]=fields[group][0];
    }
    if(!r.ok)return false;
    *out=result;return true;
}
bool sst_chr0_open(const void *container,size_t bytes,size_t offset,SstChr0 *out) {
    if(!out)return false;
    *out=(SstChr0){0};
    if(!container || bytes>SST_CHR0_MAX_BYTES || offset>bytes || bytes-offset<0x28)return false;
    Reader r={container,bytes,offset,bytes,true};uint32_t magic=u32(&r,offset),size=u32(&r,offset+4),version=u32(&r,offset+8);
    if(magic!=UINT32_C(0x43485230) || version<3 || version>5 || size<0x28 || size>bytes-offset)return false;
    r.hi=offset+size;size_t header=offset+(version==5?0x18:0x14);
    SstChr0 view={.data=container,.bytes=bytes,.offset=offset,.end=r.hi};
    view.dictionary=relative(&r,offset,u32(&r,offset+16));
    view.duration=u16(&r,header+8);view.joints=u16(&r,header+10);
    view.loop_mode=u32(&r,header+12);view.scaling_rule=u32(&r,header+16);
    uint32_t dic_size=u32(&r,view.dictionary),count=u32(&r,view.dictionary+4);
    if(!r.ok || !view.joints || view.joints>SST_CHR0_MAX_JOINTS || count!=view.joints ||
       view.loop_mode>1 || view.scaling_rule>2 || dic_size<24+16*count || !range(&r,view.dictionary,dic_size))return false;
    SstResourceSrt identity={{1,1,1},{0,0,0},{0,0,0},0},ignored;
    char names[SST_CHR0_MAX_JOINTS][SST_CHR0_NAME_BYTES];
    for(unsigned i=0;i<view.joints;i++) {
        size_t node_at;
        if(!node(&view,i,&node_at,names[i]) || !sample(&view,i,0,&identity,&ignored))return false;
        for(unsigned j=0;j<i;j++)if(!strcmp(names[i],names[j]))return false;
    }
    *out=view;return true;
}
bool sst_chr0_joint_name(const SstChr0 *view,unsigned index,char *out,size_t capacity) {
    if(!out || !capacity)return false;
    out[0]=0;char value[SST_CHR0_NAME_BYTES];size_t at;
    if(!node(view,index,&at,value) || strlen(value)>=capacity)return false;
    memcpy(out,value,strlen(value)+1);return true;
}
bool sst_chr0_sample(const SstChr0 *view,unsigned index,float frame,const SstResourceSrt *bind,SstResourceSrt *out) {
    if(!out)return false;
    SstResourceSrt copy;if(bind){copy=*bind;bind=&copy;}
    *out=(SstResourceSrt){0};
    return sample(view,index,frame,bind,out);
}
bool sst_resource_pose_world(const int16_t *parents,const SstResourceSrt *local,
                             unsigned count,float (*out)[12],unsigned capacity) {
    if(!out || !capacity || capacity>SST_CHR0_MAX_JOINTS)return false;
    float matrices[SST_CHR0_MAX_JOINTS][12];
    if(!parents || !local || !count || count>capacity)goto fail;
    for(unsigned i=0;i<count;i++) {
        int parent=parents[i];
        if(parent< -1 || (parent>=0 && (unsigned)parent>=i))goto fail;
        double angle[3];
        for(unsigned axis=0;axis<3;axis++) {
            if(!isfinite(local[i].scale[axis]) || local[i].scale[axis]!=1.f ||
               !isfinite(local[i].rotation[axis]) || fabsf(local[i].rotation[axis])>1e6f ||
               !isfinite(local[i].translation[axis]) || fabsf(local[i].translation[axis])>1e6f)goto fail;
            angle[axis]=fmod((double)local[i].rotation[axis],360.0)*0.017453292519943295;
        }
        double sx=sin(angle[0]),cx=cos(angle[0]),sy=sin(angle[1]),cy=cos(angle[1]),sz=sin(angle[2]),cz=cos(angle[2]);
        double transform[12]={cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,local[i].translation[0],
            sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,local[i].translation[1],
            -sy,cy*sx,cy*cx,local[i].translation[2]};
        for(unsigned row=0;row<3;row++)for(unsigned column=0;column<4;column++) {
            double value=transform[4*row+column];
            if(parent>=0) {
                value=column==3?matrices[parent][4*row+3]:0;
                for(unsigned k=0;k<3;k++)value+=matrices[parent][4*row+k]*transform[4*k+column];
            }
            if(!isfinite(value) || fabs(value)>1e6)goto fail;
            matrices[i][4*row+column]=(float)value;
        }
    }
    memset(out,0,(size_t)capacity*sizeof *out);memcpy(out,matrices,(size_t)count*sizeof *out);return true;
fail:
    memset(out,0,(size_t)capacity*sizeof *out);return false;
}

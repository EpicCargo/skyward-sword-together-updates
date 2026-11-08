// SPDX-License-Identifier: MIT
// Adapted narrowly from the user-provided MIT handoff's validated child-offset
// algorithm. No peer resolver, observer-pose fallback, or native pointers retained.
#include "sst_hand.h"
#include "sst_avatar.h"
#include <math.h>
#include <string.h>

#define ENTRIES 512
#define MESHES 64
#define TRANSLATION_TOLERANCE .05f
#define ROTATION_TOLERANCE .1f
typedef struct {
    uint16_t mesh, clip, action, context; // fills the existing two-byte padding
    unsigned bucket;
    float frame;
    SstHandOffset offset;
    uint32_t tick;
    unsigned confirmations;
    bool blocked;
    uint64_t used;
} Entry;
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(Entry)==96,"bounded hand entry metadata");
#endif
typedef struct {
    uint16_t body;
    uint32_t normals;
    float translation[3];
    unsigned mismatches;
    uint32_t mismatch_tick;
    bool has_mismatch_tick;
    bool known, blocked;
} Parent;
static Entry entries[ENTRIES];
static Parent parents[4][MESHES]; // independent identities0 / drawn2 / carry4 / carry+drawn6
_Static_assert(sizeof parents<=8192,"bounded four-context parent metadata");
static uint64_t serial, placements;

static bool finite_matrix(const float *m) {
    if (!m) return false;
    for (unsigned i=0;i<12;i++) if (!isfinite(m[i])) return false;
    return true;
}
static bool local_valid(const float *m) {
    if (!finite_matrix(m)) return false;
    for (unsigned i=0;i<12;i++) if (fabsf(m[i]) > (i%4==3 ? 256.f : 64.f)) return false;
    float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    return isfinite(det) && fabsf(det)>=1e-3f && fabsf(det)<=1e3f;
}
static bool multiply(const float *a,const float *b,float *out) {
    if (!finite_matrix(a) || !finite_matrix(b)) return false;
    float result[12];
    for (unsigned r=0;r<3;r++) {
        for (unsigned c=0;c<3;c++) result[r*4+c]=a[r*4]*b[c]+a[r*4+1]*b[4+c]+a[r*4+2]*b[8+c];
        result[r*4+3]=a[r*4]*b[3]+a[r*4+1]*b[7]+a[r*4+2]*b[11]+a[r*4+3];
    }
    if (!finite_matrix(result)) return false;
    memcpy(out,result,sizeof result);return true;
}
static bool invert(const float *m,float *out) {
    if (!finite_matrix(m)) return false;
    float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    float A=e*i-f*h,B=f*g-d*i,C=d*h-e*g,det=a*A+b*B+c*C;
    if (!isfinite(det) || fabsf(det)<1e-6f) return false;
    float inverse[9]={A/det,(c*h-b*i)/det,(b*f-c*e)/det,B/det,(a*i-c*g)/det,(c*d-a*f)/det,C/det,(b*g-a*h)/det,(a*e-b*d)/det};
    for (unsigned r=0;r<3;r++) {
        for(unsigned c2=0;c2<3;c2++)out[r*4+c2]=inverse[r*3+c2];
        out[r*4+3]=-(inverse[r*3]*m[3]+inverse[r*3+1]*m[7]+inverse[r*3+2]*m[11]);
    }
    return finite_matrix(out);
}
static bool finite_clip(uint16_t clip) { return clip==42 || clip==44 || clip==45 || (clip>=191 && clip<=210); }
static int context_slot(uint16_t context) {
    if(!context)return 0;
    if(context==SST_EQUIP_SWORD_DRAWN)return 1;
    if(context==SST_POSE_CONTEXT_SMALL_CARRY)return 2;
    if(context==(SST_POSE_CONTEXT_SMALL_CARRY|SST_EQUIP_SWORD_DRAWN))return 3;
    return -1;
}
static bool ready(const Entry *e) {
    int context=context_slot(e->context);
    return e->mesh && context>=0 && !e->blocked && e->confirmations>=2 &&
        !parents[context][e->mesh].blocked && !parents[context][e->mesh].mismatches;
}
void sst_hand_reset(void) { memset(entries,0,sizeof entries);memset(parents,0,sizeof parents);serial=placements=0; }
void sst_hand_forget_clip(uint16_t clip) {
    for(unsigned i=0;i<ENTRIES;i++)if(entries[i].mesh && entries[i].clip==clip)memset(&entries[i],0,sizeof entries[i]);
}
void sst_hand_observe(uint16_t mesh,const SstPose *pose,uint16_t body,size_t pn,size_t nn,
                      const float *joint_matrix,const float *model) {
    uint16_t joint;uint32_t layout;
    if(mesh>=MESHES || !pose || !body || !sst_avatar_hand_parent(mesh,&joint,&layout) || pn!=layout ||
       nn>0x10000 || nn%48 || !isfinite(pose->anim_frame) || pose->anim_frame<0 || pose->anim_frame>65536)return;
    uint16_t context=pose->equip_flags&SST_POSE_IDENTITY_MASK;
    int bank=context_slot(context);if(bank<0)return;
    Parent *parent=&parents[bank][mesh];
    if(parent->blocked || (parent->known && (parent->body!=body || parent->normals!=nn)))return;
    float inverse[12],local[12];
    if(!invert(joint_matrix,inverse) || !multiply(inverse,model,local) || !local_valid(local))return;
    float translation[3]={local[3],local[7],local[11]};
    if(!parent->known) {
        parent->known=true;parent->body=body;parent->normals=(uint32_t)nn;
        memcpy(parent->translation,translation,sizeof translation);
    } else {
        bool mismatch=false;
        for(unsigned k=0;k<3;k++)if(fabsf(translation[k]-parent->translation[k])>TRANSLATION_TOLERANCE)mismatch=true;
        if(mismatch) {
            if(!parent->has_mismatch_tick || parent->mismatch_tick!=pose->tick) {
                parent->has_mismatch_tick=true;parent->mismatch_tick=pose->tick;
                if(++parent->mismatches>=3)parent->blocked=true;
            }
            return;
        }
        parent->mismatches=0;parent->has_mismatch_tick=false;
    }
    unsigned bucket=(unsigned)(pose->anim_frame/(finite_clip(pose->anim_id)?1.5f:3.f));
    Entry *slot=NULL,*unused=NULL,*oldest=NULL;
    for(unsigned i=0;i<ENTRIES;i++) {
        Entry *e=&entries[i];
        if(!e->mesh) {if(!unused)unused=e;continue;}
        if(e->mesh==mesh && e->clip==pose->anim_id && e->action==pose->action && e->context==context && e->bucket==bucket &&
           e->offset.body==body && e->offset.positions==pn && e->offset.normals==nn) {slot=e;break;}
        if(!oldest || e->used<oldest->used)oldest=e;
    }
    if(!slot) {
        slot=unused?unused:oldest;
        *slot=(Entry){.mesh=mesh,.clip=pose->anim_id,.action=pose->action,.context=context,.bucket=bucket,.frame=pose->anim_frame,
            .offset={.body=body,.joint=joint,.positions=(uint32_t)pn,.normals=(uint32_t)nn},
            .tick=pose->tick,.confirmations=1,.used=++serial};
        memcpy(slot->offset.local,local,sizeof local);return;
    }
    if(slot->blocked || slot->tick==pose->tick)return;
    slot->tick=pose->tick;
    for(unsigned k=0;k<12;k++)if(fabsf(local[k]-slot->offset.local[k])>(k%4==3?TRANSLATION_TOLERANCE:ROTATION_TOLERANCE)) {
        slot->blocked=true;return;
    }
    if(slot->confirmations<255)slot->confirmations++;
}
bool sst_hand_lookup(uint16_t mesh,const SstPose *pose,bool loop,float length,SstHandOffset *out) {
    if(!out)return false;
    *out=(SstHandOffset){0};
    if(mesh>=MESHES || !pose || !isfinite(pose->anim_frame) || pose->anim_frame<0 || pose->anim_frame>65536)return false;
    uint16_t context=pose->equip_flags&SST_POSE_IDENTITY_MASK;
    int bank=context_slot(context);if(bank<0 || parents[bank][mesh].blocked)return false;
    Entry *best=NULL;float distance=INFINITY;
    for(unsigned i=0;i<ENTRIES;i++) {
        Entry *e=&entries[i];if(e->mesh!=mesh || e->clip!=pose->anim_id || e->action!=pose->action || e->context!=context || !ready(e))continue;
        float delta=e->frame-pose->anim_frame;
        if(loop && isfinite(length) && length>0)delta=remainderf(delta,length);
        if(fabsf(delta)<=(finite_clip(pose->anim_id)?2.f:4.5f) && fabsf(delta)<distance) {best=e;distance=fabsf(delta);}
    }
    if(!best)return false;
    best->used=++serial;*out=best->offset;return true;
}
bool sst_hand_compose(const float *joint,const float *local,float *out) {
    return out && local_valid(local) && multiply(joint,local,out);
}
void sst_hand_note_placement(void) { placements++; }
SstHandStats sst_hand_stats(void) {
    SstHandStats out={.placements=placements};
    for(unsigned i=0;i<ENTRIES;i++)if(entries[i].mesh) {out.entries++;if(ready(&entries[i]))out.ready++;if(entries[i].blocked)out.blocked++;}
    for(unsigned context=0;context<4;context++)
        for(unsigned i=0;i<MESHES;i++)if(parents[context][i].blocked)out.blocked_parents++;
    return out;
}

static uint64_t hand_hash_bytes(uint64_t hash,const void *data,size_t size) {
    const unsigned char *bytes=data;
    for(size_t i=0;i<size;i++)hash=(hash^bytes[i])*UINT64_C(1099511628211);
    return hash;
}
uint64_t sst_hand_teaching_hash(void) {
    // Named fields only: padding and LRU/placement activity are not teaching.
    uint64_t hash=UINT64_C(14695981039346656037);
#define HASH_FIELD(object,field) hash=hand_hash_bytes(hash,&(object)->field,sizeof (object)->field)
    for(unsigned i=0;i<ENTRIES;i++) {
        const Entry *e=&entries[i];
        HASH_FIELD(e,mesh);HASH_FIELD(e,clip);HASH_FIELD(e,action);HASH_FIELD(e,context);
        HASH_FIELD(e,bucket);HASH_FIELD(e,frame);HASH_FIELD(e,tick);
        HASH_FIELD(e,confirmations);HASH_FIELD(e,blocked);
        HASH_FIELD(e,offset.body);HASH_FIELD(e,offset.joint);
        HASH_FIELD(e,offset.positions);HASH_FIELD(e,offset.normals);HASH_FIELD(e,offset.local);
    }
    for(unsigned bank=0;bank<4;bank++)for(unsigned i=0;i<MESHES;i++) {
        const Parent *p=&parents[bank][i];
        HASH_FIELD(p,body);HASH_FIELD(p,normals);HASH_FIELD(p,translation);
        HASH_FIELD(p,mismatches);HASH_FIELD(p,mismatch_tick);HASH_FIELD(p,has_mismatch_tick);
        HASH_FIELD(p,known);HASH_FIELD(p,blocked);
    }
#undef HASH_FIELD
    return hash;
}

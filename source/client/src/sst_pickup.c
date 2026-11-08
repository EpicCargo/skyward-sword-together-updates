#include "sst_pickup.h"
#include "sst_chest.h"
#include <limits.h>
#include <string.h>

static size_t stage_length(const SstScene *s) {
    if (!s) return 0;
    size_t n=0;
    while(n<8 && s->stage[n]) {
        unsigned char c=(unsigned char)s->stage[n];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'))return 0;
        n++;
    }
    return n && n<=7 && !s->stage[n]?n:0;
}
static bool same_area(const SstScene *a,const SstScene *b) {
    return !strcmp(a->stage,b->stage) && a->room==b->room && a->layer==b->layer;
}
static SstScene normalized(const SstScene *s) {
    SstScene result={0};size_t n=stage_length(s);
    if(n) {memcpy(result.stage,s->stage,n);result.room=s->room;result.layer=s->layer;}
    return result; // entrance and ignored padding are not part of area identity
}
bool sst_pickup_item(uint16_t item,unsigned *counter,int *amount) {
    if(item==2 || item==3 || item==4 || item==32 || item==33 || (item>=161 && item<=176))
        return sst_chest_item(item,counter,amount);
    if(item!=34 && item!=6 && item!=7 && item!=8 && item!=40 && item!=41 && item!=42 && item!=57 && item!=60 &&
       (item<141 || item>152))return false;
    if(counter)*counter=UINT_MAX;
    if(amount)*amount=0;
    return true;
}
bool sst_pickup_valid(const SstPickup *p) {
    if(!p || !stage_length(&p->scene) || !sst_pickup_item(p->item,NULL,NULL) ||
       (p->placed_id==0xffff?!p->token:p->token!=0))return false;
    unsigned raw=p->params&511,kind=(p->params>>20)&15;
    if(kind==1)return p->placed_id==0xffff && (p->params&~511u)==SST_PICKUP_DROP_PARAMS && raw==p->item;
    unsigned flag=(p->params>>10)&255;
    if(kind || (flag!=255 && (flag>=128 || p->placed_id==0xffff)))return false;
    if(raw==p->item)return true;
    // These are the proved native class0 rarity resolutions, not a general raw-item mismatch.
    if(raw==61 || raw==62)return p->item==165 || p->item==166 || p->item==168;
    if(raw==63)return p->item==171 || p->item==173;
    if(raw==64)return p->item==174 || p->item==175 || p->item==176;
    return false;
}
bool sst_pickup_commit_valid(const SstPickupCommit *c) {
    return c && c->epoch && c->from && sst_pickup_valid(&c->pickup);
}
bool sst_pickup_key_equal(const SstPickup *a,uint16_t from_a,const SstPickup *b,uint16_t from_b) {
    if(!a || !b || !stage_length(&a->scene) || !stage_length(&b->scene) || !same_area(&a->scene,&b->scene))return false;
    if(a->placed_id==0xffff || b->placed_id==0xffff)
        return a->placed_id==0xffff && b->placed_id==0xffff && from_a && from_a==from_b && a->token && a->token==b->token;
    return a->placed_id==b->placed_id && a->params==b->params;
}
static bool same_reward(const SstPickup *a,const SstPickup *b) {
    if(a->placed_id!=b->placed_id || a->params!=b->params || a->token!=b->token)return false;
    if(a->item==b->item)return true;
    // A fixed rarity source can roll a different approved material on two PCs.
    // Consumption is shared; each collector keeps its own native outcome.
    unsigned raw=a->params&511;
    return raw>=61 && raw<=64 && ((a->params>>20)&15)==0 && sst_pickup_valid(a) && sst_pickup_valid(b);
}

typedef struct { uint8_t *p;size_t at; } Writer;
static void put8(Writer *w,uint8_t v){w->p[w->at++]=v;}
static void put16(Writer *w,uint16_t v){put8(w,(uint8_t)v);put8(w,(uint8_t)(v>>8));}
static void put32(Writer *w,uint32_t v){put16(w,(uint16_t)v);put16(w,(uint16_t)(v>>16));}
static void put64(Writer *w,uint64_t v){put32(w,(uint32_t)v);put32(w,(uint32_t)(v>>32));}
typedef struct { const uint8_t *p;size_t at,n;bool bad; } Reader;
static uint8_t get8(Reader *r){if(r->at>=r->n){r->bad=true;return 0;}return r->p[r->at++];}
static uint16_t get16(Reader *r){uint16_t lo=get8(r);return lo|(uint16_t)get8(r)<<8;}
static uint32_t get32(Reader *r){uint32_t lo=get16(r);return lo|(uint32_t)get16(r)<<16;}
static uint64_t get64(Reader *r){uint64_t lo=get32(r);return lo|(uint64_t)get32(r)<<32;}
static void write_pickup(Writer *w,const SstPickup *p) {
    size_t n=stage_length(&p->scene);put8(w,(uint8_t)n);
    for(size_t i=0;i<n;i++)put8(w,(uint8_t)p->scene.stage[i]);
    put8(w,p->scene.room);put8(w,p->scene.layer);put8(w,p->scene.entrance);
    put16(w,p->placed_id);put32(w,p->params);put16(w,p->item);put32(w,p->sequence);put64(w,p->token);
}
static bool read_pickup(Reader *r,SstPickup *p) {
    unsigned n=get8(r);if(!n || n>7)return false;
    for(unsigned i=0;i<n;i++){p->scene.stage[i]=(char)get8(r);if(!p->scene.stage[i])return false;}
    p->scene.room=get8(r);p->scene.layer=get8(r);p->scene.entrance=get8(r);
    p->placed_id=get16(r);p->params=get32(r);p->item=get16(r);p->sequence=get32(r);p->token=get64(r);
    return !r->bad && sst_pickup_valid(p);
}
size_t sst_pickup_encode(const SstPickup *p,uint8_t *out,size_t capacity) {
    if(!out || !sst_pickup_valid(p))return 0;
    size_t need=25+stage_length(&p->scene);if(capacity<need)return 0;
    Writer w={.p=out};put8(&w,0);write_pickup(&w,p);return w.at;
}
bool sst_pickup_decode(const uint8_t *data,size_t size,SstPickup *out) {
    if(!data || !out || size<26 || size>SST_PICKUP_WIRE_MAX)return false;
    Reader r={.p=data,.n=size};SstPickup p={0};
    if(get8(&r)!=0 || !read_pickup(&r,&p) || r.bad || r.at!=size)return false;
    *out=p;return true;
}
size_t sst_pickup_commit_encode(const SstPickupCommit *c,uint8_t *out,size_t capacity) {
    if(!out || !sst_pickup_commit_valid(c))return 0;
    size_t need=31+stage_length(&c->pickup.scene);if(capacity<need)return 0;
    Writer w={.p=out};put8(&w,c->snapshot?2:1);write_pickup(&w,&c->pickup);put32(&w,c->epoch);put16(&w,c->from);return w.at;
}
bool sst_pickup_commit_decode(const uint8_t *data,size_t size,SstPickupCommit *out) {
    if(!data || !out || size<32 || size>SST_PICKUP_WIRE_MAX)return false;
    Reader r={.p=data,.n=size};SstPickupCommit c={0};unsigned kind=get8(&r);
    if((kind!=1 && kind!=2) || !read_pickup(&r,&c.pickup))return false;
    c.snapshot=kind==2;c.epoch=get32(&r);c.from=get16(&r);
    if(r.bad || r.at!=size || !sst_pickup_commit_valid(&c))return false;
    *out=c;return true;
}

void sst_pickup_host_reset(SstPickupHost *h){if(h)memset(h,0,sizeof *h);}
static int host_area(const SstPickupHost *h,const SstScene *scene) {
    for(unsigned i=0;i<SST_PICKUP_AREAS;i++)if(h->areas[i].used && same_area(&h->areas[i].scene,scene))return (int)i;
    return -1;
}
uint32_t sst_pickup_host_begin(SstPickupHost *h,const SstScene *scene) {
    if(!h || !stage_length(scene))return 0;
    int old=host_area(h,scene);if(old>=0)return h->areas[old].epoch;
    if(h->serial==UINT32_MAX)return 0; // never reuse an epoch after counter wrap
    for(unsigned i=0;i<SST_PICKUP_AREAS;i++)if(!h->areas[i].used) {
        h->areas[i]=(SstPickupArea){.used=true,.scene=normalized(scene),.epoch=++h->serial};return h->areas[i].epoch;
    }
    return 0;
}
void sst_pickup_host_prune(SstPickupHost *h,const SstScene *occupied,size_t count) {
    if(!h || count>SST_MAX_PLAYERS || (count && !occupied))return;
    for(size_t i=0;i<count;i++)if(!stage_length(&occupied[i]))return;
    for(unsigned i=0;i<SST_PICKUP_AREAS;i++)if(h->areas[i].used) {
        bool found=false;for(size_t j=0;j<count;j++)if(same_area(&h->areas[i].scene,&occupied[j])){found=true;break;}
        if(found)continue;
        for(unsigned j=0;j<SST_PICKUP_RECORDS;j++)if(h->records[j].used && h->records[j].area==i)memset(&h->records[j],0,sizeof h->records[j]);
        memset(&h->areas[i],0,sizeof h->areas[i]);
    }
}
SstPickupClaim sst_pickup_host_claim(SstPickupHost *h,uint16_t from,const SstPickup *p,SstPickupCommit *out) {
    if(!h || !from || !out || !sst_pickup_valid(p))return SST_PICKUP_CLAIM_INVALID;
    int area=host_area(h,&p->scene);if(area<0)return SST_PICKUP_CLAIM_INVALID;
    SstPickupHostRecord *empty=NULL;
    for(unsigned i=0;i<SST_PICKUP_RECORDS;i++) {
        SstPickupHostRecord *r=&h->records[i];
        if(!r->used){if(!empty)empty=r;continue;}
        if(r->area!=(unsigned)area || !sst_pickup_key_equal(&r->commit.pickup,r->commit.from,p,from))continue;
        if(!same_reward(&r->commit.pickup,p))return SST_PICKUP_CLAIM_INVALID;
        *out=r->commit;out->snapshot=true;return SST_PICKUP_CLAIM_DUPLICATE;
    }
    if(!empty || h->areas[area].count>=SST_PICKUPS_PER_AREA)return SST_PICKUP_CLAIM_FULL;
    SstPickupCommit commit={.pickup=*p,.epoch=h->areas[area].epoch,.from=from};
    *empty=(SstPickupHostRecord){.used=true,.area=(uint16_t)area,.commit=commit};h->areas[area].count++;
    *out=commit;return SST_PICKUP_CLAIM_NEW;
}
void sst_pickup_host_snapshots(const SstPickupHost *h,const SstScene *scene,SstPickupFn fn,void *user) {
    if(!h || !fn || !stage_length(scene))return;
    int area=host_area(h,scene);if(area<0)return;
    for(unsigned i=0;i<SST_PICKUP_RECORDS;i++)if(h->records[i].used && h->records[i].area==(unsigned)area) {
        SstPickupCommit c=h->records[i].commit;c.snapshot=true;fn(&c,user);
    }
}

void sst_pickup_reset(SstPickupState *s){if(s)memset(s,0,sizeof *s);}
bool sst_pickup_begin(SstPickupState *s,const SstScene *scene,uint32_t epoch) {
    if(!s || !stage_length(scene))return false;
    s->initialized=true;s->scene=normalized(scene);s->epoch=epoch;return true;
}
static SstPickupClientRecord *client_record(SstPickupState *s,const SstPickupCommit *c,SstPickupClientRecord **empty) {
    for(unsigned i=0;i<SST_PICKUP_RECORDS;i++) {
        SstPickupClientRecord *r=&s->records[i];if(!r->used){if(empty && !*empty)*empty=r;continue;}
        if(r->commit.epoch==c->epoch && sst_pickup_key_equal(&r->commit.pickup,r->source,&c->pickup,c->from))return r;
    }
    return NULL;
}
static bool same_commit(const SstPickupClientRecord *r,const SstPickupCommit *c) {
    return same_reward(&r->commit.pickup,&c->pickup) && (!r->has_commit ||
        (r->commit.from==c->from && r->commit.pickup.sequence==c->pickup.sequence));
}
bool sst_pickup_local(SstPickupState *s,uint16_t mine,const SstPickup *p,uint64_t now) {
    if(!s || !s->initialized || !mine || !sst_pickup_valid(p) || !same_area(&s->scene,&p->scene))return false;
    SstPickupCommit key={.pickup=*p,.epoch=s->epoch,.from=mine};SstPickupClientRecord *empty=NULL;
    SstPickupClientRecord *r=client_record(s,&key,&empty);
    if(r && (!same_reward(&r->commit.pickup,p) || r->applied))return false;
    if(!r) {if(!empty)return false;r=empty;*r=(SstPickupClientRecord){.used=true,.source=mine,.commit=key};}
    r->local=true;r->local_source=mine;r->local_sequence=p->sequence;r->local_ms=now;r->applied=true;
    return true;
}
SstPickupDecision sst_pickup_accept(SstPickupState *s,const SstPickupCommit *c,uint64_t now) {
    if(!s || !sst_pickup_commit_valid(c))return SST_PICKUP_INVALID;
    SstPickupClientRecord *empty=NULL,*r=client_record(s,c,&empty);
    if(r) {
        if(!same_commit(r,c))return SST_PICKUP_INVALID;
        if(!r->has_commit) {r->commit=*c;r->source=c->from;r->has_commit=true;r->applied=true;return SST_PICKUP_LOCAL;}
        // A late snapshot must not cancel live consumption processing waiting for native safety.
        return r->applied?SST_PICKUP_SEEN:SST_PICKUP_NEW;
    }
    // Local native pickup can beat the host context or another collector's winning claim.
    for(unsigned i=0;i<SST_PICKUP_RECORDS;i++) {
        SstPickupClientRecord *local=&s->records[i];
        if(!local->used || !local->local || local->has_commit ||
           !sst_pickup_key_equal(&local->commit.pickup,local->source,&c->pickup,c->from) ||
           !same_reward(&local->commit.pickup,&c->pickup))continue;
        bool own=local->local_source==c->from && local->local_sequence==c->pickup.sequence;
        // Native safety can delay draining a legitimate commit for a long cutscene.
        // Compare collection with authenticated arrival time, not the eventual apply frame.
        uint64_t at=c->received_ms && c->received_ms<=now?c->received_ms:now;
        uint64_t gap=at>=local->local_ms?at-local->local_ms:local->local_ms-at;
        bool recent=now>=local->local_ms && gap<=SST_PICKUP_LOCAL_FRESH_MS;
        if(!own && !(local->commit.epoch==c->epoch || (!local->commit.epoch && recent)))continue;
        local->commit=*c;local->source=c->from;local->has_commit=true;local->applied=true;return SST_PICKUP_LOCAL;
    }
    if(!empty)return SST_PICKUP_FULL;
    *empty=(SstPickupClientRecord){.used=true,.source=c->from,.has_commit=true,.applied=c->snapshot,.commit=*c};
    return c->snapshot?SST_PICKUP_SNAPSHOT:SST_PICKUP_NEW;
}
bool sst_pickup_applied(SstPickupState *s,const SstPickupCommit *c) {
    if(!s || !sst_pickup_commit_valid(c) || c->snapshot)return false;
    SstPickupClientRecord *r=client_record(s,c,NULL);
    if(!r || !r->has_commit || !same_commit(r,c))return false;
    r->applied=true;return true;
}
void sst_pickup_pending(const SstPickupState *s,SstPickupFn fn,void *user) {
    if(!s || !fn)return;
    for(unsigned i=0;i<SST_PICKUP_RECORDS;i++) {
        const SstPickupClientRecord *r=&s->records[i];
        if(r->used && r->has_commit && !r->applied && !r->commit.snapshot)fn(&r->commit,user);
    }
}

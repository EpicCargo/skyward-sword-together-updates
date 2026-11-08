// Pure consumed-pickup codecs, bounded receipts and inventory-neutral consumption decisions.
// Link src/sst_pickup.c + src/sst_chest.c; no game, sockets, addresses or actor instances.
#include "../src/sst_pickup.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SstScene scene(unsigned room) {
    return (SstScene){.stage="F001r",.room=(uint8_t)room,.layer=1,.entrance=5};
}
static SstPickup pickup(unsigned room,unsigned placed,unsigned seq,uint16_t item) {
    return (SstPickup){.scene=scene(room),.placed_id=(uint16_t)placed,
        .params=item|(255u<<10),.item=item,.sequence=seq};
}
static SstPickupCommit commit(unsigned room,unsigned placed,unsigned epoch,unsigned seq) {
    return (SstPickupCommit){.pickup=pickup(room,placed,seq,4),.epoch=epoch,.from=2};
}
static uint64_t hash(const void *data,size_t n) {
    const unsigned char *p=data;uint64_t h=UINT64_C(14695981039346656037);
    for(size_t i=0;i<n;i++)h=(h^p[i])*UINT64_C(1099511628211);return h;
}
static unsigned callbacks;
static bool expect_snapshot;
static uint32_t expect_epoch;
static void record(const SstPickupCommit *c,void *user) {
    (void)user;callbacks++;assert(c->snapshot==expect_snapshot);
    if(expect_epoch)assert(c->epoch==expect_epoch);
}
static unsigned pending(const SstPickupState *s) {
    callbacks=0;expect_snapshot=false;expect_epoch=0;sst_pickup_pending(s,record,NULL);return callbacks;
}

static void registry_and_codecs(void) {
    for(unsigned id=0;id<512;id++) {
        bool counted=id==2 || id==3 || id==4 || id==32 || id==33 || (id>=161 && id<=176);
        bool expected=counted || id==34 || id==6 || id==7 || id==8 || id==40 || id==41 ||
            id==42 || id==57 || id==60 || (id>=141 && id<=152);
        unsigned counter=999;int amount=999;assert(sst_pickup_item(id,&counter,&amount)==expected);
        if(!expected){assert(counter==999 && amount==999);continue;}
        if(!counted){assert(counter==UINT_MAX && amount==0);}
        else if(id>=161){assert(counter==22+id-161 && amount==1);}
        else assert(counter==0 && amount==(id==2?1:id==3?5:id==4?20:id==32?100:300));
        SstPickup selected=pickup(1,12,id,(uint16_t)id),roundtrip={0};
        uint8_t wire[SST_PICKUP_WIRE_MAX];size_t count=sst_pickup_encode(&selected,wire,sizeof wire);
        assert(sst_pickup_valid(&selected) && count && sst_pickup_decode(wire,count,&roundtrip));
        assert(roundtrip.item==id && roundtrip.params==selected.params);
    }
    SstPickup p=pickup(1,12,0x12345678,176),decoded={0};
    uint8_t bytes[SST_PICKUP_WIRE_MAX+1];memset(bytes,0xa5,sizeof bytes);
    size_t n=sst_pickup_encode(&p,bytes,sizeof bytes);assert(n && n<=SST_PICKUP_WIRE_MAX && bytes[0]==0);
    assert(sst_pickup_decode(bytes,n,&decoded) && decoded.sequence==p.sequence && decoded.params==p.params);
    assert(sst_pickup_key_equal(&p,2,&decoded,3)); // placed identity is sender-independent
    SstPickup before;memset(&before,0x5a,sizeof before);
    for(size_t i=0;i<n;i++){decoded=before;assert(!sst_pickup_decode(bytes,i,&decoded));assert(!memcmp(&decoded,&before,sizeof before));}
    decoded=before;assert(!sst_pickup_decode(bytes,n+1,&decoded));assert(!memcmp(&decoded,&before,sizeof before));
    uint8_t untouched[SST_PICKUP_WIRE_MAX];memset(untouched,0xa5,sizeof untouched);
    assert(!sst_pickup_encode(&p,untouched,n-1));for(unsigned i=0;i<sizeof untouched;i++)assert(untouched[i]==0xa5);

    SstPickupCommit c={.pickup=p,.epoch=0x01020304,.from=0x1234,.presence_generation=77,.received_ms=9999},out;
    n=sst_pickup_commit_encode(&c,bytes,sizeof bytes);assert(n && bytes[0]==1);
    assert(sst_pickup_commit_decode(bytes,n,&out) && out.epoch==c.epoch && out.from==c.from && !out.snapshot);
    assert(!out.presence_generation && !out.received_ms);
    assert(bytes[n-6]==4 && bytes[n-5]==3 && bytes[n-4]==2 && bytes[n-3]==1 && bytes[n-2]==0x34 && bytes[n-1]==0x12);
    assert(!sst_pickup_decode(bytes,n,&decoded));
    c.snapshot=true;n=sst_pickup_commit_encode(&c,bytes,sizeof bytes);assert(bytes[0]==2);
    assert(sst_pickup_commit_decode(bytes,n,&out) && out.snapshot);
    SstPickupCommit old;memset(&old,0x5a,sizeof old);
    for(size_t i=0;i<n;i++){out=old;assert(!sst_pickup_commit_decode(bytes,i,&out));assert(!memcmp(&out,&old,sizeof old));}
    out=old;assert(!sst_pickup_commit_decode(bytes,n+1,&out));assert(!memcmp(&out,&old,sizeof old));
    bytes[0]=3;assert(!sst_pickup_commit_decode(bytes,n,&out));
    n=sst_pickup_encode(&p,bytes,sizeof bytes);assert(!sst_pickup_commit_decode(bytes,n,&out));
    bytes[1]=8;assert(!sst_pickup_decode(bytes,n,&decoded));

    p=pickup(1,12,1,4);assert(sst_pickup_valid(&p));
    p.params|=1u<<20;assert(!sst_pickup_valid(&p));p=pickup(1,12,1,4);
    p.params=(p.params&~(255u<<10))|(128u<<10);assert(!sst_pickup_valid(&p));p=pickup(1,12,1,4);
    p.params^=1;assert(!sst_pickup_valid(&p));p=pickup(1,12,1,4);
    p.token=1;assert(!sst_pickup_valid(&p));p.placed_id=0xffff;assert(sst_pickup_valid(&p));
    SstPickup other=p;assert(!sst_pickup_key_equal(&p,2,&other,3));assert(sst_pickup_key_equal(&p,2,&other,2));
    other.token++;assert(!sst_pickup_key_equal(&p,2,&other,2));p.token=0;assert(!sst_pickup_valid(&p));
    p=pickup(1,12,1,4);p.scene.stage[0]='!';assert(!sst_pickup_valid(&p));
    memset(p.scene.stage,'A',sizeof p.scene.stage);assert(!sst_pickup_valid(&p));

    // Class1 is ONLY the native spawnDrop mask, only dynamic source tokens, and exact raw item.
    for(unsigned id=0;id<512;id++) {
        if(!sst_pickup_item((uint16_t)id,NULL,NULL))continue;
        p=pickup(1,0xffff,1,(uint16_t)id);p.token=UINT64_C(0xfedcba9876543210);
        p.params=SST_PICKUP_DROP_PARAMS|id;assert(sst_pickup_valid(&p));
        n=sst_pickup_encode(&p,bytes,sizeof bytes);assert(n && sst_pickup_decode(bytes,n,&decoded));
        assert(decoded.placed_id==0xffff && decoded.token==p.token && decoded.item==id);
        for(unsigned bit=9;bit<32;bit++)if(bit!=20) {
            SstPickup bad=p;bad.params^=1u<<bit;assert(!sst_pickup_valid(&bad));
        }
        SstPickup bad=p;bad.placed_id=12;bad.token=0;assert(!sst_pickup_valid(&bad));
        bad=p;bad.token=0;assert(!sst_pickup_valid(&bad));
    }
    // Every permitted rarity result is explicit; all other raw/final pairs fail closed.
    for(unsigned raw=61;raw<=64;raw++)for(unsigned item=0;item<512;item++) {
        bool allowed=raw<=62?(item==165 || item==166 || item==168):
            raw==63?(item==171 || item==173):(item==174 || item==175 || item==176);
        p=pickup(1,12,1,(uint16_t)item);p.params=raw|(255u<<10);
        assert(sst_pickup_valid(&p)==allowed);
        if(allowed){n=sst_pickup_encode(&p,bytes,sizeof bytes);assert(n && sst_pickup_decode(bytes,n,&decoded));}
        p.placed_id=0xffff;p.token=1;p.params=SST_PICKUP_DROP_PARAMS|raw;
        assert(!sst_pickup_valid(&p)); // no generic mismatch in dynamic class1 drops
    }
    for(unsigned flag=0;flag<128;flag++) {
        p=pickup(1,12,1,4);p.params=4|(flag<<10);assert(sst_pickup_valid(&p));
        n=sst_pickup_encode(&p,bytes,sizeof bytes);assert(n && sst_pickup_decode(bytes,n,&decoded));
        assert(decoded.placed_id==12 && ((decoded.params>>10)&255)==flag);
        p.placed_id=0xffff;p.token=1;assert(!sst_pickup_valid(&p));
        p=pickup(1,12,1,165);p.params=61|(flag<<10);assert(sst_pickup_valid(&p));
        p.item=164;assert(!sst_pickup_valid(&p)); // permanence cannot broaden rarity resolution
    }
    for(unsigned flag=128;flag<255;flag++) {
        p=pickup(1,12,1,4);p.params=4|(flag<<10);assert(!sst_pickup_valid(&p));
        p.placed_id=0xffff;p.token=1;assert(!sst_pickup_valid(&p));
        p=pickup(1,12,1,165);p.params=61|(flag<<10);assert(!sst_pickup_valid(&p));
    }
    for(unsigned kind=2;kind<16;kind++) {
        p=pickup(1,0xffff,1,4);p.token=1;p.params=4|(255u<<10)|(kind<<20);assert(!sst_pickup_valid(&p));
    }
}

static void host_ledger(void) {
    SstPickupHost *h=calloc(1,sizeof *h);assert(h);SstScene a=scene(1);
    uint32_t epoch=sst_pickup_host_begin(h,&a);assert(epoch && sst_pickup_host_begin(h,&a)==epoch);
    SstPickup p=pickup(1,9,1,4);SstPickupCommit c,dup;
    assert(sst_pickup_host_claim(h,2,&p,&c)==SST_PICKUP_CLAIM_NEW && !c.snapshot && c.epoch==epoch);
    p.sequence=99;assert(sst_pickup_host_claim(h,3,&p,&dup)==SST_PICKUP_CLAIM_DUPLICATE);
    assert(dup.snapshot && dup.from==2 && dup.pickup.sequence==1);
    callbacks=0;expect_snapshot=true;expect_epoch=epoch;sst_pickup_host_snapshots(h,&a,record,NULL);assert(callbacks==1);
    a.entrance++;assert(sst_pickup_host_begin(h,&a)==epoch);
    SstScene occupied[]={a,a};sst_pickup_host_prune(h,occupied,2);
    assert(sst_pickup_host_claim(h,2,&p,&dup)==SST_PICKUP_CLAIM_DUPLICATE);
    uint64_t old=hash(h,sizeof *h);SstScene invalid={0};sst_pickup_host_prune(h,&invalid,1);assert(hash(h,sizeof *h)==old);
    SstPickupCommit sentinel;memset(&sentinel,0x5a,sizeof sentinel);dup=sentinel;p.item=6;
    assert(sst_pickup_host_claim(h,2,&p,&dup)==SST_PICKUP_CLAIM_INVALID && hash(h,sizeof *h)==old);
    assert(!memcmp(&dup,&sentinel,sizeof dup));

    p=pickup(1,400,3,165);p.params=61|(255u<<10);
    assert(sst_pickup_host_claim(h,2,&p,&c)==SST_PICKUP_CLAIM_NEW);
    p.item=166;old=hash(h,sizeof *h);dup=sentinel;
    assert(sst_pickup_host_claim(h,3,&p,&dup)==SST_PICKUP_CLAIM_DUPLICATE && hash(h,sizeof *h)==old);
    assert(dup.snapshot && dup.from==2 && dup.pickup.item==165 && dup.pickup.sequence==3);
    p.item=164;dup=sentinel;
    assert(sst_pickup_host_claim(h,3,&p,&dup)==SST_PICKUP_CLAIM_INVALID && hash(h,sizeof *h)==old);
    assert(!memcmp(&dup,&sentinel,sizeof dup)); // unapproved generic outcome still fails closed
    sst_pickup_host_prune(h,NULL,0);a=scene(1);uint32_t next=sst_pickup_host_begin(h,&a);assert(next>epoch);
    p=pickup(1,9,1,4);assert(sst_pickup_host_claim(h,2,&p,&c)==SST_PICKUP_CLAIM_NEW && c.epoch==next);

    // Dynamic source tokens cannot collide across authenticated collector IDs.
    p=pickup(1,0xffff,2,161);p.token=UINT64_C(0x123456789abcdef0);
    assert(sst_pickup_host_claim(h,2,&p,&c)==SST_PICKUP_CLAIM_NEW);
    assert(sst_pickup_host_claim(h,3,&p,&c)==SST_PICKUP_CLAIM_NEW);
    old=hash(h,sizeof *h);p.item=162;p.params=162|(255u<<10);dup=sentinel;
    assert(sst_pickup_host_claim(h,2,&p,&dup)==SST_PICKUP_CLAIM_INVALID && hash(h,sizeof *h)==old);
    assert(!memcmp(&dup,&sentinel,sizeof dup));

    // Every active area keeps256 keys and the full4096 never evicts a receipt.
    sst_pickup_host_reset(h);SstScene all[SST_PICKUP_AREAS];
    for(unsigned i=0;i<SST_PICKUP_AREAS;i++) {
        all[i]=scene(i);assert(sst_pickup_host_begin(h,&all[i]));
        for(unsigned k=0;k<SST_PICKUPS_PER_AREA;k++) {
            p=pickup(i,k,k+1,2);assert(sst_pickup_host_claim(h,2,&p,&c)==SST_PICKUP_CLAIM_NEW);
        }
        p=pickup(i,300,301,2);old=hash(h,sizeof *h);dup=sentinel;
        assert(sst_pickup_host_claim(h,2,&p,&dup)==SST_PICKUP_CLAIM_FULL && hash(h,sizeof *h)==old);
        assert(!memcmp(&dup,&sentinel,sizeof dup));
    }
    a=scene(99);old=hash(h,sizeof *h);assert(!sst_pickup_host_begin(h,&a) && hash(h,sizeof *h)==old);
    sst_pickup_host_prune(h,all,SST_PICKUP_AREAS);assert(hash(h,sizeof *h)==old);
    sst_pickup_host_prune(h,all+1,SST_PICKUP_AREAS-1);assert(sst_pickup_host_begin(h,&a));
    p=pickup(1,0,1,2);assert(sst_pickup_host_claim(h,9,&p,&dup)==SST_PICKUP_CLAIM_DUPLICATE);
    a=scene(1);callbacks=0;expect_snapshot=true;expect_epoch=0;sst_pickup_host_snapshots(h,&a,record,NULL);assert(callbacks==256);
    sst_pickup_host_reset(h);h->serial=UINT32_MAX;assert(!sst_pickup_host_begin(h,&a));free(h);
}

static void personal_decisions(void) {
    SstPickupState *s=calloc(1,sizeof *s);assert(s);SstScene local=scene(9);
    assert(sst_pickup_begin(s,&local,50));SstPickupCommit c=commit(1,9,77,1);
    assert(sst_pickup_accept(s,&c,1000)==SST_PICKUP_NEW && pending(s)==1);
    assert(sst_pickup_accept(s,&c,1001)==SST_PICKUP_NEW); // consumption processing not marked complete
    SstPickupCommit snapshot=c;snapshot.snapshot=true;
    assert(sst_pickup_accept(s,&snapshot,1002)==SST_PICKUP_NEW && pending(s)==1);
    SstScene elsewhere=scene(5);assert(sst_pickup_begin(s,&elsewhere,88) && pending(s)==1);
    SstPickupCommit second=commit(2,9,78,1);assert(sst_pickup_accept(s,&second,1003)==SST_PICKUP_NEW && pending(s)==2);
    assert(sst_pickup_applied(s,&c) && sst_pickup_applied(s,&c) && pending(s)==1);
    assert(sst_pickup_accept(s,&c,1004)==SST_PICKUP_SEEN);
    SstPickupCommit fresh_epoch=c;fresh_epoch.epoch=79;
    assert(sst_pickup_accept(s,&fresh_epoch,1005)==SST_PICKUP_NEW && pending(s)==2);
    snapshot=commit(3,10,80,2);snapshot.snapshot=true;
    assert(sst_pickup_accept(s,&snapshot,1006)==SST_PICKUP_SNAPSHOT && pending(s)==2);
    snapshot.snapshot=false;assert(sst_pickup_accept(s,&snapshot,1007)==SST_PICKUP_SEEN);
    uint64_t old=hash(s,sizeof *s);SstPickupCommit bad=c;bad.epoch=0;
    assert(sst_pickup_accept(s,&bad,1008)==SST_PICKUP_INVALID && hash(s,sizeof *s)==old);
    bad=c;bad.from=3;assert(sst_pickup_accept(s,&bad,1008)==SST_PICKUP_INVALID && hash(s,sizeof *s)==old);
    bad=c;bad.pickup.sequence++;assert(!sst_pickup_applied(s,&bad) && hash(s,sizeof *s)==old);

    // The collector records its own native award before even having a host epoch.
    sst_pickup_reset(s);local=scene(1);assert(sst_pickup_begin(s,&local,0));
    SstPickup p=pickup(1,9,11,4);assert(sst_pickup_local(s,7,&p,1000));
    assert(sst_pickup_begin(s,&elsewhere,88));c=commit(1,9,77,55); // another player won a simultaneous claim
    assert(sst_pickup_accept(s,&c,1100)==SST_PICKUP_LOCAL && !pending(s));
    assert(sst_pickup_accept(s,&c,1101)==SST_PICKUP_SEEN);
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,0));assert(sst_pickup_local(s,7,&p,1000));
    c=commit(1,9,77,55);c.received_ms=1100;
    assert(sst_pickup_accept(s,&c,100000)==SST_PICKUP_LOCAL); // recipient cutscene delayed the drain
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,0));assert(sst_pickup_local(s,7,&p,1000));
    c=commit(1,9,77,55);c.received_ms=900;
    assert(sst_pickup_accept(s,&c,1100)==SST_PICKUP_LOCAL); // wire arrival can precede native local proof
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,0));assert(sst_pickup_local(s,7,&p,1000));
    c=commit(1,9,77,11);c.from=7;
    assert(sst_pickup_accept(s,&c,1000+SST_PICKUP_LOCAL_FRESH_MS+1)==SST_PICKUP_LOCAL);
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,77));assert(sst_pickup_local(s,7,&p,1000));
    c=commit(1,9,77,55);assert(sst_pickup_accept(s,&c,100000)==SST_PICKUP_LOCAL); // known equal epoch
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,0));assert(sst_pickup_local(s,7,&p,1000));
    c=commit(1,9,78,55);assert(sst_pickup_accept(s,&c,1000+SST_PICKUP_LOCAL_FRESH_MS+1)==SST_PICKUP_NEW);
    assert(pending(s)==1); // expired proof cannot suppress processing a different new incarnation
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,77));c=commit(1,9,77,55);
    assert(sst_pickup_accept(s,&c,1000)==SST_PICKUP_NEW);assert(sst_pickup_local(s,7,&p,1100));
    assert(!pending(s) && sst_pickup_accept(s,&c,1101)==SST_PICKUP_SEEN);

    // Simultaneous collectors can roll different approved materials from one fixed generic source.
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,0));
    p=pickup(1,400,11,166);p.params=61|(255u<<10);assert(sst_pickup_local(s,7,&p,1000));
    c=(SstPickupCommit){.pickup=pickup(1,400,55,165),.epoch=77,.from=2};
    c.pickup.params=61|(255u<<10);
    assert(sst_pickup_accept(s,&c,1100)==SST_PICKUP_LOCAL && !pending(s));
    assert(sst_pickup_accept(s,&c,1101)==SST_PICKUP_SEEN);
    sst_pickup_reset(s);assert(sst_pickup_begin(s,&local,77));
    assert(sst_pickup_accept(s,&c,1000)==SST_PICKUP_NEW);
    assert(sst_pickup_local(s,7,&p,1100) && !pending(s));
    assert(sst_pickup_accept(s,&c,1101)==SST_PICKUP_SEEN);
    SstPickupCommit rejected=c;rejected.pickup.item=164;old=hash(s,sizeof *s);
    assert(sst_pickup_accept(s,&rejected,1102)==SST_PICKUP_INVALID && hash(s,sizeof *s)==old);

    // Global dedup evidence survives arbitrary local area changes and fails closed at capacity.
    sst_pickup_reset(s);
    for(unsigned i=0;i<SST_PICKUP_RECORDS;i++){c=commit(i%16,i,1+i%16,i);assert(sst_pickup_accept(s,&c,i)==SST_PICKUP_NEW);}
    assert(pending(s)==SST_PICKUP_RECORDS);old=hash(s,sizeof *s);c=commit(1,6000,99,5000);
    assert(sst_pickup_accept(s,&c,5000)==SST_PICKUP_FULL && hash(s,sizeof *s)==old);
    local=scene(15);assert(sst_pickup_begin(s,&local,999) && pending(s)==SST_PICKUP_RECORDS);free(s);
}

int main(void) {
    registry_and_codecs();host_ledger();personal_decisions();
    puts("pickup state PASS: consumed-only registry, native drop/rarity/class guards, strict codecs, non-evicting claims, source-token identity, cross-area consumption records and local/snapshot dedup");
    return 0;
}

// Experimental portable CPU metadata/scopes only. No game, assets or native memory.
// Suggested: clang -std=c11 -O1 -g -Wall -Wextra -fsanitize=address,undefined \
//   tests/origin_test.c src/sst_origin.c -lpthread -o build/origin_test
#include "../src/sst_origin.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static SstOriginProof proof_at(uint64_t at) {
    SstOriginProof p;memset(&p,0,sizeof p);
    p.identity.actor_address=UINT64_C(0x1000);p.identity.actor_instance=0;
    strcpy(p.identity.scene.stage,"SYNTH");p.identity.scene.room=2;p.identity.scene.layer=1;p.identity.scene.entrance=3;
    p.identity.model_address=UINT64_C(0x2000);p.identity.object_address=UINT64_C(0x3000);
    p.identity.resource_address=UINT64_C(0x4000);p.identity.role=0;
    p.identity.model_registration_generation=1;p.recorded_ms=at;return p;
}
static void cleared(const SstOriginProof *p) {
    SstOriginProof zero;memset(&zero,0,sizeof zero);assert(!memcmp(p,&zero,sizeof zero));
}
static void same_proof(const SstOriginProof *a,const SstOriginProof *b) {
    const SstOriginIdentity *x=&a->identity,*y=&b->identity;
    assert(x->actor_address==y->actor_address && x->actor_instance==y->actor_instance);
    assert(!strcmp(x->scene.stage,y->scene.stage) && x->scene.room==y->scene.room &&
           x->scene.layer==y->scene.layer && x->scene.entrance==y->scene.entrance);
    assert(x->model_address==y->model_address && x->object_address==y->object_address &&
           x->resource_address==y->resource_address && x->role==y->role &&
           x->model_registration_generation==y->model_registration_generation);
    assert(a->recorded_ms==b->recorded_ms);
}
static void forced(SstOriginMarker m) {
    assert(sst_origin_is_forced(m) && sst_origin_native_hidden(m) && !sst_origin_learning_allowed(m));
}
static void unforced(SstOriginMarker m) {
    assert(!sst_origin_is_forced(m) && !sst_origin_native_hidden(m) && sst_origin_learning_allowed(m));
}
static void marker_same(SstOriginMarker a,SstOriginMarker b) {assert(!memcmp(&a,&b,sizeof a));}
static SstOriginMarker publish(SstOriginProof *p,uint64_t now) {
    SstOriginMarker out=sst_origin_none();assert(sst_origin_publish(p,now,&out));forced(out);return out;
}
static void copied_and_nonconsuming(void) {
    sst_origin_reset();sst_origin_scope_clear();
    SstOriginProof p=proof_at(100),expected=p,out;SstOriginMarker m=publish(&p,100);
    // Addresses are deliberately synthetic/unmapped: the module never dereferences them.
    memset(&p,0xee,sizeof p);
    for(unsigned i=0;i<5;i++) {
        memset(&out,0xee,sizeof out);assert(sst_origin_read(m,100+i,&out));same_proof(&out,&expected);
        assert(sst_origin_peek(m,&expected.identity,100+i,&out));same_proof(&out,&expected);
    }
    SstOriginIdentity current=expected.identity;
    memset(current.scene.stage+6,0xab,sizeof current.scene.stage-6); // ignore bytes AFTER boundedNUL
    assert(sst_origin_peek(m,&current,105,&out));
    same_proof(&out,&expected);
    // Strict peek allows the current input to be the output's identity subobject.
    out=expected;assert(sst_origin_peek(m,&out.identity,106,&out));same_proof(&out,&expected);
    assert(!sst_origin_read(m,106,NULL));assert(!sst_origin_peek(m,&expected.identity,106,NULL));
    assert(sst_origin_read(m,106,&out));
}
static void reset_expiry_and_future(void) {
    sst_origin_reset();SstOriginProof p=proof_at(1000),out;SstOriginMarker old=publish(&p,1000);
    SstOriginScope scope;assert(sst_origin_scope_push(old,&scope));
    sst_origin_reset();forced(old);marker_same(sst_origin_current(),old);
    memset(&out,0xee,sizeof out);assert(!sst_origin_read(old,1000,&out));cleared(&out);
    assert(!sst_origin_peek(old,&p.identity,1000,&out));cleared(&out);
    SstOriginMarker fresh=publish(&p,1000);assert(memcmp(&fresh,&old,sizeof fresh));
    assert(sst_origin_read(fresh,1000,&out));
    assert(sst_origin_scope_pop(&scope));unforced(sst_origin_current());
    assert(sst_origin_read(fresh,2000,&out)); // inclusive1000ms bound, nonconsuming
    assert(sst_origin_peek(fresh,&p.identity,2000,&out));
    assert(!sst_origin_read(fresh,2001,&out));cleared(&out);forced(fresh);
    assert(!sst_origin_read(fresh,1999,&out)); // expiry cannot revive on clock rollback
    p=proof_at(5000);fresh=publish(&p,5000);
    assert(!sst_origin_read(fresh,4999,&out));cleared(&out);forced(fresh);
    assert(!sst_origin_read(fresh,5000,&out)); // future-clock rejection invalidates lease
    fresh=publish(&p,5000);assert(!sst_origin_peek(fresh,&p.identity,6001,&out));cleared(&out);
    assert(!sst_origin_read(fresh,5000,&out));forced(fresh);
}
static void identity_changes(void) {
    for(unsigned change=0;change<11;change++) {
        SstOriginProof p=proof_at(1000),out;SstOriginMarker m=publish(&p,1000);
        SstOriginIdentity current=p.identity;
        switch(change) {
        case 0:current.actor_address++;break;
        case 1:current.actor_instance++;break;
        case 2:strcpy(current.scene.stage,"OTHER");break;
        case 3:current.scene.room++;break;
        case 4:current.scene.layer++;break;
        case 5:current.scene.entrance++;break;
        case 6:current.model_address++;break;
        case 7:current.object_address++;break;
        case 8:current.resource_address++;break;
        case 9:current.role++;break;
        case 10:current.model_registration_generation++;break;
        }
        // Metadata read is historical data only; it does not authorize current state.
        assert(sst_origin_read(m,1000,&out));
        assert(!sst_origin_peek(m,&current,1000,&out));cleared(&out);forced(m);
        assert(!sst_origin_read(m,1000,&out)); // observed mismatch cannot revive byABA
        assert(!sst_origin_peek(m,&p.identity,1000,&out));cleared(&out);
    }
    SstOriginProof p=proof_at(1000),out;SstOriginMarker m=publish(&p,1000);
    assert(!sst_origin_peek(m,NULL,1000,&out));cleared(&out);forced(m);
}
static void bounded_overwrite(void) {
    sst_origin_reset();SstOriginProof p=proof_at(100),out;
    SstOriginMarker first=sst_origin_none(),second=sst_origin_none(),last=sst_origin_none();
    for(unsigned i=0;i<SST_ORIGIN_CAPACITY+1;i++) {
        SstOriginMarker m=publish(&p,100);
        if(!i)first=m;
        if(i==1)second=m;
        last=m;
    }
    SstOriginStats s=sst_origin_stats();assert(s.capacity==8192 && s.live_leases==8192);
    assert(s.slot_storage_bytes<4u*1024u*1024u);
    assert(memcmp(&first,&last,sizeof first));assert(!sst_origin_read(first,100,&out));cleared(&out);forced(first);
    assert(sst_origin_read(second,100,&out) && sst_origin_read(last,100,&out));
    assert(sst_origin_peek(last,&p.identity,100,&out));assert(sst_origin_peek(last,&p.identity,100,&out));
    sst_origin_reset();assert(!sst_origin_stats().live_leases);
}
static void combine_and_tls_scopes(void) {
    sst_origin_scope_clear();SstOriginProof p=proof_at(100);SstOriginMarker a=publish(&p,100),b=publish(&p,100);
    SstOriginMarker none=sst_origin_none(),unknown=sst_origin_forced_unresolved();
    SstOriginMarker ua=sst_origin_unowned(a),ub=sst_origin_unowned(b),u0=sst_origin_unowned(none);
    unforced(none);unforced(ua);unforced(u0);forced(unknown);
    marker_same(sst_origin_combine(none,a),a);marker_same(sst_origin_combine(a,none),a);
    marker_same(sst_origin_combine(a,b),a);marker_same(sst_origin_combine(a,ua),a); // immutable intrinsicFORCE
    marker_same(sst_origin_combine(ua,a),none); // only matching NONZERO parent barrier clears
    marker_same(sst_origin_combine(ub,a),a);marker_same(sst_origin_combine(u0,a),a);
    marker_same(sst_origin_combine(ua,unknown),unknown);marker_same(sst_origin_combine(u0,unknown),unknown);
    marker_same(sst_origin_combine(none,ua),ua);
    SstOriginScope outer,empty,unowned,nested_unowned,inner,wrong_barrier;
    assert(sst_origin_scope_push(a,&outer));marker_same(sst_origin_current(),a);
    assert(sst_origin_scope_push(none,&empty));marker_same(sst_origin_current(),a);assert(sst_origin_scope_pop(&empty));
    assert(sst_origin_scope_push(ua,&unowned));unforced(sst_origin_current());
    marker_same(sst_origin_current(),ua);assert(memcmp(&ua,&none,sizeof ua));
    marker_same(sst_origin_combine(sst_origin_current(),a),none);
    marker_same(sst_origin_unowned(sst_origin_current()),ua); // nested unowned retains blockedF
    assert(sst_origin_scope_push(sst_origin_unowned(sst_origin_current()),&nested_unowned));
    marker_same(sst_origin_current(),ua);
    SstOriginMarker recorded_barrier=sst_origin_current();
    assert(sst_origin_scope_push(none,&empty));marker_same(sst_origin_current(),ua);
    assert(sst_origin_scope_pop(&empty));assert(sst_origin_scope_pop(&nested_unowned));
    assert(sst_origin_scope_push(b,&inner));marker_same(sst_origin_current(),b);
    assert(sst_origin_scope_push(ua,&wrong_barrier));marker_same(sst_origin_current(),ua);
    marker_same(sst_origin_combine(sst_origin_current(),b),b);assert(sst_origin_scope_pop(&wrong_barrier));
    assert(sst_origin_scope_pop(&inner));marker_same(sst_origin_current(),ua);
    assert(sst_origin_scope_pop(&unowned));marker_same(sst_origin_current(),a);
    assert(sst_origin_scope_pop(&outer));marker_same(sst_origin_current(),none);
    marker_same(sst_origin_combine(recorded_barrier,a),none); // bytes survive native scope return
    assert(sst_origin_scope_push(unknown,&outer));
    assert(sst_origin_scope_push(sst_origin_unowned(unknown),&unowned));
    marker_same(sst_origin_current(),sst_origin_unowned(unknown));
    forced(sst_origin_combine(sst_origin_current(),unknown)); // barrier0 cannot clear callerFORCE0
    assert(sst_origin_scope_pop(&unowned));forced(sst_origin_current());assert(sst_origin_scope_pop(&outer));
    marker_same(sst_origin_current(),none);
    // A reset inside the native scope erases replay only, not native/learning policy.
    assert(sst_origin_scope_push(a,&outer));sst_origin_reset();marker_same(sst_origin_current(),a);
    SstOriginProof out;assert(!sst_origin_read(a,100,&out));cleared(&out);forced(sst_origin_current());
    assert(sst_origin_scope_pop(&outer));unforced(sst_origin_current());
}
static void malformed_and_failed_publication(void) {
    sst_origin_reset();SstOriginProof p=proof_at(100),out;SstOriginMarker valid=publish(&p,100),m;
    m=valid;m.bytes[0]^=0x80;forced(m);assert(!sst_origin_read(m,100,&out));cleared(&out);
    marker_same(sst_origin_combine(m,valid),sst_origin_forced_unresolved());
    m=valid;m.bytes[15]^=0x80;forced(m);assert(!sst_origin_read(m,100,&out));cleared(&out);
    m=sst_origin_none();m.bytes[8]=1;forced(m);assert(!sst_origin_read(m,100,&out));cleared(&out);
    m=sst_origin_none();unforced(m);assert(!sst_origin_read(m,100,&out));cleared(&out);
    m=sst_origin_unowned(valid);unforced(m);assert(!sst_origin_read(m,100,&out));cleared(&out);
    uint32_t live=sst_origin_stats().live_leases;
    for(unsigned failure=0;failure<9;failure++) {
        SstOriginProof bad=p;
        switch(failure) {
        case 0:bad.identity.actor_address=0;break;
        case 1:bad.identity.model_address=0;break;
        case 2:bad.identity.object_address=0;break;
        case 3:bad.identity.resource_address=0;break;
        case 4:bad.identity.model_registration_generation=0;break;
        case 5:memset(bad.identity.scene.stage,'X',sizeof bad.identity.scene.stage);break;
        case 6:bad.identity.scene.stage[0]=0;break;
        case 7:bad.recorded_ms=101;break; // future
        case 8:bad.recorded_ms=0;break; // expired at1001
        }
        m=valid;assert(!sst_origin_publish(&bad,failure==8?1001:100,&m));forced(m);
        marker_same(m,sst_origin_forced_unresolved());assert(!sst_origin_read(m,100,&out));cleared(&out);
        assert(sst_origin_stats().live_leases==live && sst_origin_read(valid,100,&out));
    }
    assert(!sst_origin_publish(NULL,100,&m));forced(m);assert(!sst_origin_publish(&p,100,NULL));
    assert(sst_origin_read(valid,100,&out));
}
typedef struct { unsigned id; } Worker;
static void *worker(void *opaque) {
    Worker *arg=opaque;assert(!sst_origin_is_forced(sst_origin_current()));
    for(unsigned i=0;i<96;i++) {
        SstOriginProof p=proof_at(1000),out;p.identity.actor_address+=arg->id*4096+i;
        p.identity.role=(uint16_t)arg->id;SstOriginMarker m=publish(&p,1000);
        SstOriginScope force,barrier;assert(sst_origin_scope_push(m,&force));marker_same(sst_origin_current(),m);
        assert(sst_origin_scope_push(sst_origin_unowned(m),&barrier));unforced(sst_origin_current());
        assert(sst_origin_scope_pop(&barrier));forced(sst_origin_current());
        for(unsigned check=0;check<3;check++) {
            assert(sst_origin_read(m,1000,&out));assert(out.identity.actor_address==p.identity.actor_address);
            assert(sst_origin_peek(m,&p.identity,1000,&out));assert(out.identity.role==arg->id);
        }
        assert(sst_origin_scope_pop(&force));unforced(sst_origin_current());
    }
    return NULL;
}
static void concurrency(void) {
    sst_origin_reset();SstOriginProof p=proof_at(1000);SstOriginMarker outer=publish(&p,1000);
    SstOriginScope scope;assert(sst_origin_scope_push(outer,&scope));
    pthread_t threads[4];Worker work[4];
    for(unsigned i=0;i<4;i++){work[i].id=i;assert(!pthread_create(&threads[i],NULL,worker,&work[i]));}
    for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    marker_same(sst_origin_current(),outer);assert(sst_origin_scope_pop(&scope));
    assert(sst_origin_stats().live_leases==385);sst_origin_reset();
}
static void sticky_unsupported_and_scope_failure(void) {
    SstOriginProof p=proof_at(100),out;SstOriginMarker m=publish(&p,100);SstOriginScope scope;
    assert(sst_origin_scope_push(m,&scope));sst_origin_latch_unsupported();assert(sst_origin_unsupported());
    assert(sst_origin_stats().unsupported);forced(m);assert(sst_origin_read(m,100,&out)); // data, notauthorization
    assert(!sst_origin_peek(m,&p.identity,100,&out));cleared(&out);
    sst_origin_reset();assert(sst_origin_unsupported());forced(sst_origin_current());
    assert(!sst_origin_read(m,100,&out));cleared(&out);assert(sst_origin_scope_pop(&scope));
    assert(!sst_origin_publish(&p,100,&m));forced(m);
    // Incorrect receipt ordering is fail-closed, never a return to unowned.
    SstOriginScope a,b;assert(sst_origin_scope_push(sst_origin_none(),&a));
    assert(sst_origin_scope_push(sst_origin_forced_unresolved(),&b));
    assert(!sst_origin_scope_pop(&a));forced(sst_origin_current());assert(!sst_origin_scope_pop(&b));
    // Simulate abandoned/unwound callers; no actual native callback is active.
    sst_origin_scope_clear();unforced(sst_origin_current());
    assert(!sst_origin_scope_pop(&b));forced(sst_origin_current());sst_origin_scope_clear();
}
static void render_intent(void) {
    SstOriginProof p=proof_at(100);SstOriginMarker mark=publish(&p,100);
    sst_origin_set_replay_requested(true);assert(sst_origin_replay_requested());
    sst_origin_set_replay_requested(false);assert(!sst_origin_replay_requested());
    assert(sst_origin_native_hidden(mark) && !sst_origin_learning_allowed(mark));
    sst_origin_set_replay_requested(true);sst_origin_reset();assert(!sst_origin_replay_requested());
    assert(sst_origin_native_hidden(mark) && !sst_origin_learning_allowed(mark));
}
int main(void) {
    render_intent();
    assert(sizeof(SstOriginMarker)==16);
    copied_and_nonconsuming();reset_expiry_and_future();identity_changes();bounded_overwrite();
    combine_and_tls_scopes();malformed_and_failed_publication();concurrency();sticky_unsupported_and_scope_failure();
    SstOriginStats s=sst_origin_stats();
    assert(s.capacity==8192 && s.slot_storage_bytes<4u*1024u*1024u);
    printf("origin CPU-only PASS: capacity%u storage%zuB, immutable FORCE + bounded nonconsuming metadata; no native/GPU reuse claim\n",s.capacity,s.slot_storage_bytes);
    return 0;
}

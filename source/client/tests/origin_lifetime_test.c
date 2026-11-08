// CPU-only admission/drain race. Does not claim GPU or native allocation coverage.
#include "../src/sst_origin.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static pthread_mutex_t mu=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv=PTHREAD_COND_INITIALIZER;
static bool admitted,release_reader,source_reader,batch_reader;
static _Atomic bool revoker_started,revoker_done,waiter_started,waiter_done;
static void *reader(void *unused) {
    (void)unused;
    if(batch_reader){assert(sst_origin_batch_enter());assert(sst_origin_replay_enter());sst_origin_replay_leave();}
    else assert(source_reader?sst_origin_source_enter():sst_origin_replay_enter());
    pthread_mutex_lock(&mu);admitted=true;pthread_cond_broadcast(&cv);
    while(!release_reader)pthread_cond_wait(&cv,&mu);
    pthread_mutex_unlock(&mu);if(batch_reader)sst_origin_batch_leave();else if(source_reader)sst_origin_source_leave();else sst_origin_replay_leave();return NULL;
}
static void *revoker(void *unused) {
    (void)unused;atomic_store(&revoker_started,true);sst_origin_revoke();atomic_store(&revoker_done,true);return NULL;
}
static void *waiter(void *unused) {
    (void)unused;atomic_store(&waiter_started,true);assert(!sst_origin_replay_enter());atomic_store(&waiter_done,true);return NULL;
}
static void wait_true(_Atomic bool *value) {
    const struct timespec step={.tv_nsec=1000000};
    for(unsigned i=0;i<3000 && !atomic_load(value);i++)nanosleep(&step,NULL);
    assert(atomic_load(value));
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"nested")) {
        assert(sst_origin_replay_enter());assert(!sst_origin_replay_enter());assert(sst_origin_unsupported());
        sst_origin_replay_leave();sst_origin_revoke();assert(!sst_origin_replay_enter());
        puts("replay gate nested admission PASS: no recursive mutex deadlock, permanent stop");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"boundary")) {
        assert(!sst_origin_lifetime_armed());sst_origin_lifecycle_begin();
        assert(!sst_origin_source_enter()); // unarmed mutation was already in progress
        assert(sst_origin_lifetime_armed() && sst_origin_unsupported());
        sst_origin_lifecycle_end();sst_origin_reset();assert(!sst_origin_replay_enter());
        puts("pre-first-publication mutation PASS: first source admission cannot race through active native boundary");return 0;
    }
    if(argc==2 && !strcmp(argv[1],"observation")) {
        assert(!sst_origin_observation_enter());sst_origin_lifecycle_begin();
        sst_origin_lifecycle_begin();assert(!sst_origin_observation_enter());sst_origin_lifecycle_end();
        assert(sst_origin_observation_enter());assert(!sst_origin_observation_enter());sst_origin_observation_leave();
        sst_origin_lifecycle_end();assert(!sst_origin_observation_enter());assert(!sst_origin_lifetime_armed());
        puts("known-mutator observation PASS: no outside/nested/overlapping admission; balanced diagnostic snapshot does not arm or rearm");return 0;
    }
    source_reader=argc==2 && !strcmp(argv[1],"source");batch_reader=argc==2 && !strcmp(argv[1],"batch");assert(argc==1 || source_reader || batch_reader);
    SstOriginProof p={.identity={.actor_address=1,.model_address=2,.object_address=3,.resource_address=4,.model_registration_generation=1,.scene={.stage="TEST"}},.recorded_ms=100},out;
    SstOriginMarker mark;assert(sst_origin_publish(&p,100,&mark));sst_origin_set_replay_requested(true);
    pthread_t a,b,c;assert(!pthread_create(&a,NULL,reader,NULL));
    pthread_mutex_lock(&mu);while(!admitted)pthread_cond_wait(&cv,&mu);pthread_mutex_unlock(&mu);
    assert(!pthread_create(&b,NULL,revoker,NULL));wait_true(&revoker_started);
    const struct timespec step={.tv_nsec=1000000};
    for(unsigned i=0;i<3000 && (!sst_origin_unsupported() || sst_origin_replay_requested());i++)nanosleep(&step,NULL);
    assert(sst_origin_unsupported() && !sst_origin_replay_requested() && !atomic_load(&revoker_done));
    // Revocation is blocked by an admitted CPU reader, but new publications and
    // replay authority are already denied. Historical diagnostic data is not authority.
    assert(!sst_origin_peek(mark,&p.identity,100,&out));
    SstOriginMarker denied;assert(!sst_origin_publish(&p,100,&denied));
    assert(!pthread_create(&c,NULL,waiter,NULL));wait_true(&waiter_started);wait_true(&waiter_done);
    pthread_mutex_lock(&mu);release_reader=true;pthread_cond_broadcast(&cv);pthread_mutex_unlock(&mu);
    assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(!pthread_join(c,NULL));
    assert(atomic_load(&revoker_done) && atomic_load(&waiter_done));
    assert(!sst_origin_read(mark,100,&out));assert(sst_origin_native_hidden(mark) && !sst_origin_learning_allowed(mark));
    sst_origin_reset();sst_origin_set_replay_requested(true);assert(!sst_origin_replay_requested());assert(sst_origin_unsupported() && !sst_origin_replay_enter());
    puts("replay admission/drain PASS: closes before drain, waits for admitted CPU work, rejects racing new work, clears leases, reset never reopens or unforces");return 0;
}

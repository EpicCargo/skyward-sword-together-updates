#include "sst_origin.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define ORIGIN_FORCE_TAG UINT64_C(0x5353544f52494601)
#define ORIGIN_UNOWNED_TAG UINT64_C(0x5353544f52494602)
_Static_assert(sizeof(SstOriginMarker)==16,"origin marker must be16 bytes");

typedef enum { ORIGIN_NONE, ORIGIN_FORCE, ORIGIN_UNOWNED } OriginKind;
typedef struct { OriginKind kind; uint64_t token; } OriginDecoded;
typedef struct { uint64_t ticket; SstOriginProof proof; } OriginLease;
static OriginLease sst_origin_lease_table[SST_ORIGIN_CAPACITY];
_Static_assert(sizeof sst_origin_lease_table<4u*1024u*1024u,"origin lease budget");
static pthread_mutex_t sst_origin_lease_mutex=PTHREAD_MUTEX_INITIALIZER;
static uint64_t sst_origin_last_ticket;
static SstOriginStats sst_origin_counters;
// Compatibility scalar for old private host probes ONLY. Never used to decide
// admission; the monotone atomic reason mask is the sole authoritative state.
__attribute__((used)) static _Atomic bool sst_origin_stop_latch;
static _Atomic uint32_t sst_origin_reason_mask;
static _Atomic bool sst_origin_render_wanted;
static pthread_mutex_t sst_origin_use_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sst_origin_use_drained=PTHREAD_COND_INITIALIZER;
static uint64_t sst_origin_users,sst_origin_mutators,sst_origin_batches,sst_origin_observers;
static _Atomic bool sst_origin_gpu_ready;
static _Thread_local bool sst_origin_batch_active;
static _Thread_local bool sst_origin_observation_active;
static _Atomic bool sst_origin_watch_armed;
static _Thread_local unsigned sst_origin_use_kind;
static _Thread_local unsigned sst_origin_mutation_depth;
static _Atomic uint64_t sst_origin_scope_serial;
static _Thread_local SstOriginMarker sst_origin_tls_marker;
static _Thread_local uint64_t sst_origin_tls_top;
static _Thread_local bool sst_origin_tls_poisoned;
static pthread_once_t sst_origin_feature_once=PTHREAD_ONCE_INIT;
static bool sst_origin_feature_value;
static void origin_feature_init(void) {
    const char *value=getenv("SST_FIRST_PERSON_AVATARS");
    sst_origin_feature_value=value && !strcmp(value,"1") && !getenv("SST_NO_PLAYER_READ");
}
bool sst_origin_feature_enabled(void){pthread_once(&sst_origin_feature_once,origin_feature_init);return sst_origin_feature_value;}
static void origin_close_known_lifecycle(void) {
    atomic_fetch_or(&sst_origin_reason_mask,SST_ORIGIN_STOP_KNOWN_LIFECYCLE);
    atomic_store(&sst_origin_stop_latch,true);atomic_store(&sst_origin_render_wanted,false);
}

static _Atomic bool sst_origin_recovery_mode;
static _Atomic uint64_t sst_origin_active_epoch,sst_origin_reset_serial;
static uint64_t sst_origin_epoch_counter;
static SstOriginCohort sst_origin_candidate,sst_origin_authority;
static SstScene sst_origin_authority_scene;
static uint64_t sst_origin_candidate_epoch,sst_origin_candidate_reset;
static bool sst_origin_candidate_valid;
static _Thread_local uint64_t sst_origin_model_top;
static _Thread_local bool sst_origin_activation_active;
SstOriginGpuRetireStats sst_origin_gpu_retire_stats;
static SstOriginGpuWait sst_origin_gpu_wait;
static void *sst_origin_gpu_wait_context;
static _Atomic bool sst_origin_gpu_retiring;
static _Thread_local bool sst_origin_in_gpu_wait;
static pthread_t sst_origin_recovery_owner;static bool sst_origin_have_owner;
static bool origin_cohort_valid(const SstOriginCohort *c);
static void origin_reset_locked(void);
static bool origin_cohort_same(const SstOriginCohort *a,const SstOriginCohort *b);
static void origin_owner_locked(void) {
    if(!atomic_load(&sst_origin_recovery_mode))return;
    if(sst_origin_have_owner && !pthread_equal(sst_origin_recovery_owner,pthread_self()))abort();
    if(!sst_origin_have_owner){sst_origin_recovery_owner=pthread_self();sst_origin_have_owner=true;}
}
static bool origin_authorized_identity(const SstOriginIdentity *p) { // lease mutex held
    if(!atomic_load(&sst_origin_recovery_mode))return true;
    uint64_t epoch=atomic_load(&sst_origin_active_epoch);unsigned role=p->role;
    const SstOriginCohort *c=&sst_origin_authority;
    return epoch && !sst_origin_hard_failed() && p->model_allocation_epoch==epoch && role<4 &&
        p->actor_address==c->actor_address && p->actor_instance==c->actor_instance &&
        p->model_address==c->models[role] && p->object_address==c->objects[role] && p->resource_address==c->resources[role] &&
        !strcmp(p->scene.stage,sst_origin_authority_scene.stage) && p->scene.room==sst_origin_authority_scene.room &&
        p->scene.layer==sst_origin_authority_scene.layer && p->scene.entrance==sst_origin_authority_scene.entrance;
}
static uint64_t origin_word(const uint8_t *p) {
    uint64_t value=0;
    for(unsigned i=0;i<8;i++)value|=(uint64_t)p[i]<<(i*8);
    return value;
}
static void origin_put(uint8_t *p,uint64_t value) {
    for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(value>>(i*8));
}
static SstOriginMarker origin_marker(OriginKind kind,uint64_t token) {
    SstOriginMarker out={{0}};
    if(kind!=ORIGIN_NONE) {
        origin_put(out.bytes,kind==ORIGIN_FORCE?ORIGIN_FORCE_TAG:ORIGIN_UNOWNED_TAG);
        origin_put(out.bytes+8,token);
    }
    return out;
}
static OriginDecoded origin_decode(SstOriginMarker marker) {
    uint64_t tag=origin_word(marker.bytes),token=origin_word(marker.bytes+8);
    if(!tag && !token)return (OriginDecoded){ORIGIN_NONE,0};
    if(tag==ORIGIN_FORCE_TAG)return (OriginDecoded){ORIGIN_FORCE,token};
    if(tag==ORIGIN_UNOWNED_TAG)return (OriginDecoded){ORIGIN_UNOWNED,token};
    // Corrupted/unrecognized nonzero metadata may never unforce a source draw.
    return (OriginDecoded){ORIGIN_FORCE,0};
}
SstOriginMarker sst_origin_none(void) {return origin_marker(ORIGIN_NONE,0);}
SstOriginMarker sst_origin_forced_unresolved(void) {return origin_marker(ORIGIN_FORCE,0);}
SstOriginMarker sst_origin_unowned(SstOriginMarker parent) {
    OriginDecoded p=origin_decode(parent);
    return origin_marker(ORIGIN_UNOWNED,p.token);
}
SstOriginMarker sst_origin_combine(SstOriginMarker recorded,SstOriginMarker invocation) {
    OriginDecoded r=origin_decode(recorded),i=origin_decode(invocation);
    if(r.kind==ORIGIN_FORCE)return origin_marker(ORIGIN_FORCE,r.token);
    if(i.kind==ORIGIN_FORCE) {
        if(r.kind==ORIGIN_UNOWNED && r.token && r.token==i.token)
            return sst_origin_none();
        return origin_marker(ORIGIN_FORCE,i.token);
    }
    if(r.kind==ORIGIN_NONE)return origin_marker(i.kind,i.token);
    return origin_marker(r.kind,r.token);
}
bool sst_origin_is_forced(SstOriginMarker marker) {return origin_decode(marker).kind==ORIGIN_FORCE;}
bool sst_origin_native_hidden(SstOriginMarker marker) {return sst_origin_is_forced(marker);}
bool sst_origin_learning_allowed(SstOriginMarker marker) {return !sst_origin_is_forced(marker);}

static bool origin_identity_valid(const SstOriginIdentity *p) {
    return p && p->actor_address && p->model_address && p->object_address && p->resource_address &&
        p->model_registration_generation && p->scene.stage[0] &&
        memchr(p->scene.stage,0,sizeof p->scene.stage)!=NULL;
}
static void origin_copy_identity(SstOriginIdentity *out,const SstOriginIdentity *p) {
    memset(out,0,sizeof *out);
    out->actor_address=p->actor_address;out->actor_instance=p->actor_instance;
    size_t n=(const char *)memchr(p->scene.stage,0,sizeof p->scene.stage)-p->scene.stage;
    memcpy(out->scene.stage,p->scene.stage,n);
    out->scene.room=p->scene.room;out->scene.layer=p->scene.layer;out->scene.entrance=p->scene.entrance;
    out->model_address=p->model_address;out->object_address=p->object_address;
    out->resource_address=p->resource_address;out->role=p->role;
    out->model_registration_generation=p->model_registration_generation;out->model_allocation_epoch=p->model_allocation_epoch;
}
static bool origin_identity_same(const SstOriginIdentity *a,const SstOriginIdentity *b) {
    return a->actor_address==b->actor_address && a->actor_instance==b->actor_instance &&
        !strcmp(a->scene.stage,b->scene.stage) && a->scene.room==b->scene.room &&
        a->scene.layer==b->scene.layer && a->scene.entrance==b->scene.entrance &&
        a->model_address==b->model_address && a->object_address==b->object_address &&
        a->resource_address==b->resource_address && a->role==b->role &&
        a->model_registration_generation==b->model_registration_generation && a->model_allocation_epoch==b->model_allocation_epoch;
}
static void origin_invalidate(OriginLease *slot) {
    if(slot->ticket) {
        memset(slot,0,sizeof *slot);
        sst_origin_counters.live_leases--;
    }
}
static OriginLease *origin_lease(SstOriginMarker marker,uint64_t now) {
    OriginDecoded d=origin_decode(marker);
    if(d.kind!=ORIGIN_FORCE || !d.token)return NULL;
    OriginLease *slot=&sst_origin_lease_table[(d.token-1)%SST_ORIGIN_CAPACITY];
    if(slot->ticket!=d.token)return NULL;
    if(now<slot->proof.recorded_ms || now-slot->proof.recorded_ms>SST_ORIGIN_MAX_AGE_MS) {
        origin_invalidate(slot);sst_origin_counters.expired++;return NULL;
    }
    return slot;
}
bool sst_origin_publish(const SstOriginProof *proof,uint64_t now,SstOriginMarker *out) {
    SstOriginProof copy;memset(&copy,0,sizeof copy);
    bool valid=proof && origin_identity_valid(&proof->identity) && now>=proof->recorded_ms &&
        now-proof->recorded_ms<=SST_ORIGIN_MAX_AGE_MS;
    if(valid){origin_copy_identity(&copy.identity,&proof->identity);copy.recorded_ms=proof->recorded_ms;}
    if(out)*out=sst_origin_forced_unresolved();
    if(valid && !sst_origin_lifetime_arm())valid=false;
    pthread_mutex_lock(&sst_origin_lease_mutex);
    if(sst_origin_last_ticket==UINT64_MAX)sst_origin_latch_unsupported();
    if(!out || !valid || sst_origin_last_ticket==UINT64_MAX || sst_origin_unsupported() || !origin_authorized_identity(&copy.identity)) {
        sst_origin_counters.publish_failures++;
        pthread_mutex_unlock(&sst_origin_lease_mutex);return false;
    }
    uint64_t ticket=++sst_origin_last_ticket;
    OriginLease *slot=&sst_origin_lease_table[(ticket-1)%SST_ORIGIN_CAPACITY];
    if(slot->ticket)sst_origin_counters.overwritten++;
    else sst_origin_counters.live_leases++;
    slot->ticket=ticket;memcpy(&slot->proof,&copy,sizeof copy);
    sst_origin_counters.published++;
    *out=origin_marker(ORIGIN_FORCE,ticket);
    pthread_mutex_unlock(&sst_origin_lease_mutex);return true;
}
bool sst_origin_read(SstOriginMarker marker,uint64_t now,SstOriginProof *out) {
    if(out)memset(out,0,sizeof *out);
    pthread_mutex_lock(&sst_origin_lease_mutex);
    OriginLease *slot=out?origin_lease(marker,now):NULL;
    bool found=slot!=NULL;
    if(found){memcpy(out,&slot->proof,sizeof *out);sst_origin_counters.reads++;}
    else sst_origin_counters.read_failures++;
    pthread_mutex_unlock(&sst_origin_lease_mutex);return found;
}
bool sst_origin_authorize(SstOriginMarker marker,uint64_t now,SstOriginProof *out) {
    if(out)memset(out,0,sizeof *out);
    pthread_mutex_lock(&sst_origin_lease_mutex);
    OriginLease *slot=out?origin_lease(marker,now):NULL;
    bool valid=slot && !sst_origin_unsupported() && origin_authorized_identity(&slot->proof.identity);
    if(valid)memcpy(out,&slot->proof,sizeof *out);
    pthread_mutex_unlock(&sst_origin_lease_mutex);return valid;
}
bool sst_origin_peek(SstOriginMarker marker,const SstOriginIdentity *current,uint64_t now,SstOriginProof *out) {
    // Permit current==&out->identity; snapshot before clearing the output.
    SstOriginIdentity copy;bool current_valid=origin_identity_valid(current);
    if(current_valid)origin_copy_identity(&copy,current);
    if(out)memset(out,0,sizeof *out);
    pthread_mutex_lock(&sst_origin_lease_mutex);
    OriginLease *slot=out?origin_lease(marker,now):NULL;
    bool found=slot && !sst_origin_unsupported() && origin_authorized_identity(&slot->proof.identity);
    if(found && (!current_valid || !origin_identity_same(&copy,&slot->proof.identity))) {
        origin_invalidate(slot);found=false;
    }
    if(found){memcpy(out,&slot->proof,sizeof *out);sst_origin_counters.peeks++;}
    else sst_origin_counters.peek_failures++;
    pthread_mutex_unlock(&sst_origin_lease_mutex);return found;
}
void sst_origin_set_replay_requested(bool wanted){
    atomic_store(&sst_origin_render_wanted,wanted && !sst_origin_unsupported());
    // STOP never clears. Close a setter that raced with latch's intent clear.
    if(sst_origin_unsupported())atomic_store(&sst_origin_render_wanted,false);
}
bool sst_origin_replay_requested(void){return atomic_load(&sst_origin_render_wanted) && !sst_origin_unsupported();}
bool sst_origin_lifetime_armed(void){return atomic_load(&sst_origin_watch_armed);}
bool sst_origin_lifetime_arm(void) {
    pthread_mutex_lock(&sst_origin_use_mutex);
    if(sst_origin_use_kind!=3)origin_owner_locked();
    atomic_store(&sst_origin_watch_armed,true);
    if(sst_origin_mutators)origin_close_known_lifecycle();
    bool allowed=!sst_origin_unsupported();
    pthread_mutex_unlock(&sst_origin_use_mutex);return allowed;
}
static bool origin_use_enter(unsigned kind) {
    if(sst_origin_use_kind){sst_origin_latch_unsupported();return false;}
    pthread_mutex_lock(&sst_origin_use_mutex);
    if(kind!=3)origin_owner_locked(); // extra-model recording legitimately runs on native G3d workers
    if(kind==2)atomic_store(&sst_origin_watch_armed,true);
    if(kind==2 && sst_origin_mutators)origin_close_known_lifecycle();
    bool allowed=!sst_origin_unsupported() && !sst_origin_mutators;
    if(allowed){if(sst_origin_users==UINT64_MAX)abort();sst_origin_users++;sst_origin_use_kind=kind;}
    pthread_mutex_unlock(&sst_origin_use_mutex);return allowed;
}
static void origin_use_leave(unsigned kind) {
    if(sst_origin_use_kind!=kind)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);
    if(!sst_origin_users)abort();
    sst_origin_use_kind=0;if(!--sst_origin_users)pthread_cond_broadcast(&sst_origin_use_drained);
    pthread_mutex_unlock(&sst_origin_use_mutex);
}
bool sst_origin_replay_enter(void){return origin_use_enter(1);}
void sst_origin_replay_leave(void){origin_use_leave(1);}
bool sst_origin_source_enter(void){return origin_use_enter(2);}
void sst_origin_source_leave(void){origin_use_leave(2);}
void sst_origin_recovery_owner_check(void){pthread_mutex_lock(&sst_origin_use_mutex);origin_owner_locked();pthread_mutex_unlock(&sst_origin_use_mutex);}
bool sst_origin_recovery_owner_current(void){pthread_mutex_lock(&sst_origin_use_mutex);bool ok=!sst_origin_recovery_enabled() || !sst_origin_have_owner || pthread_equal(sst_origin_recovery_owner,pthread_self());pthread_mutex_unlock(&sst_origin_use_mutex);return ok;}
bool sst_origin_emission_enter(void){return origin_use_enter(3);}
void sst_origin_emission_leave(void){origin_use_leave(3);}
bool sst_origin_batch_enter(void) {
    if(sst_origin_batch_active || sst_origin_use_kind || sst_origin_observation_active)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);
    origin_owner_locked();
    bool allowed=!sst_origin_unsupported() && !sst_origin_mutators;
    if(allowed){if(sst_origin_batches==UINT64_MAX)abort();sst_origin_batches++;sst_origin_batch_active=true;}
    pthread_mutex_unlock(&sst_origin_use_mutex);return allowed;
}
void sst_origin_batch_leave(void) {
    if(!sst_origin_batch_active || sst_origin_use_kind)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);
    if(!sst_origin_batches)abort();
    sst_origin_batch_active=false;if(!--sst_origin_batches)pthread_cond_broadcast(&sst_origin_use_drained);
    pthread_mutex_unlock(&sst_origin_use_mutex);
}
bool sst_origin_gpu_retirement_configure(uint64_t domain,SstOriginGpuWait wait,void *context) {
    pthread_mutex_lock(&sst_origin_use_mutex);origin_owner_locked();
    bool same=sst_origin_gpu_wait && sst_origin_gpu_wait==wait && sst_origin_gpu_wait_context==context && sst_origin_gpu_retire_stats.domain==domain;
    bool fresh=!sst_origin_gpu_wait && sst_origin_recovery_enabled() && domain && wait && !sst_origin_users && !sst_origin_batches && !sst_origin_mutators && !sst_origin_observers && !atomic_load(&sst_origin_gpu_retiring);
    if(fresh){sst_origin_gpu_wait=wait;sst_origin_gpu_wait_context=context;sst_origin_gpu_retire_stats.domain=domain;}
    pthread_mutex_unlock(&sst_origin_use_mutex);return same || fresh;
}
void sst_origin_batch_submitted(uint64_t ticket) {
    if(!sst_origin_batch_active || sst_origin_use_kind || sst_origin_observation_active)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);origin_owner_locked();
    if(!sst_origin_batches || !sst_origin_gpu_wait || !ticket || ticket<sst_origin_gpu_retire_stats.pending) {
        sst_origin_gpu_retire_stats.failures++;sst_origin_latch_unsupported();abort();
    }
    // Reports from work admitted BEFORE a closure must survive that closure.
    sst_origin_gpu_retire_stats.pending=ticket;sst_origin_gpu_retire_stats.registered_batches++;
    sst_origin_batch_active=false;if(!--sst_origin_batches)pthread_cond_broadcast(&sst_origin_use_drained);
    pthread_mutex_unlock(&sst_origin_use_mutex);
}
SstOriginGpuRetireStats sst_origin_gpu_retirement_status(void) {
    pthread_mutex_lock(&sst_origin_use_mutex);SstOriginGpuRetireStats result=sst_origin_gpu_retire_stats;pthread_mutex_unlock(&sst_origin_use_mutex);return result;
}
void sst_origin_set_gpu_guard_ready(bool ready){atomic_store(&sst_origin_gpu_ready,ready);}
bool sst_origin_gpu_guard_ready(void){return atomic_load(&sst_origin_gpu_ready) && !sst_origin_unsupported();}
bool sst_origin_observation_enter(void) {
    pthread_mutex_lock(&sst_origin_use_mutex);
    bool allowed=sst_origin_mutation_depth==1 && sst_origin_mutators==1 && !sst_origin_users && !sst_origin_batches && !sst_origin_observation_active;
    if(allowed){sst_origin_observers++;sst_origin_observation_active=true;}
    pthread_mutex_unlock(&sst_origin_use_mutex);return allowed;
}
void sst_origin_observation_leave(void) {
    if(!sst_origin_observation_active)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);
    if(!sst_origin_observers)abort();
    sst_origin_observation_active=false;if(!--sst_origin_observers)pthread_cond_broadcast(&sst_origin_use_drained);
    pthread_mutex_unlock(&sst_origin_use_mutex);
}
static void origin_drain_locked(void) {
    while(sst_origin_users || sst_origin_batches || sst_origin_observers)pthread_cond_wait(&sst_origin_use_drained,&sst_origin_use_mutex);
    if(sst_origin_gpu_retire_stats.pending>sst_origin_gpu_retire_stats.completed) {
        if(!sst_origin_gpu_wait || sst_origin_in_gpu_wait || atomic_load(&sst_origin_gpu_retiring))abort();
        origin_owner_locked();
        uint64_t target=sst_origin_gpu_retire_stats.pending,completed=0;
        SstOriginGpuWait wait=sst_origin_gpu_wait;void *context=sst_origin_gpu_wait_context;
        atomic_store(&sst_origin_gpu_retiring,true);sst_origin_in_gpu_wait=true;
        sst_origin_gpu_retire_stats.wait_calls++;sst_origin_gpu_retire_stats.last_wait_target=target;
        // Authority/mutator or hard closure stays live. No module mutex spans
        // the Vulkan wait, and no native mutation runs until it acknowledges.
        pthread_mutex_unlock(&sst_origin_use_mutex);
        bool ok=wait(context,target,&completed);
        pthread_mutex_lock(&sst_origin_use_mutex);
        if(!ok || completed<target){sst_origin_gpu_retire_stats.failures++;sst_origin_latch_unsupported();abort();}
        if(completed>sst_origin_gpu_retire_stats.completed)sst_origin_gpu_retire_stats.completed=completed;
        sst_origin_gpu_retire_stats.wait_completions++;sst_origin_in_gpu_wait=false;atomic_store(&sst_origin_gpu_retiring,false);
    }
    origin_reset_locked(); // use mutex -> lease mutex; never the reverse
}
void sst_origin_revoke(void) {
    sst_origin_latch_unsupported();sst_origin_set_replay_requested(false);
    if(sst_origin_use_kind || sst_origin_batch_active || sst_origin_observation_active || sst_origin_in_gpu_wait)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);origin_drain_locked();pthread_mutex_unlock(&sst_origin_use_mutex);
}
void sst_origin_lifecycle_begin(void) {
    if(sst_origin_use_kind || sst_origin_batch_active || sst_origin_observation_active)abort(); // never mutate underneath our own caller
    pthread_mutex_lock(&sst_origin_use_mutex);
    origin_owner_locked();
    if(sst_origin_mutators==UINT64_MAX || sst_origin_mutation_depth==UINT32_MAX)abort();
    sst_origin_mutators++;sst_origin_mutation_depth++;
    if(sst_origin_recovery_enabled())sst_origin_latch_unsupported();
    if(sst_origin_lifetime_armed()) {
        origin_close_known_lifecycle();sst_origin_set_replay_requested(false);origin_drain_locked();
    } else while(sst_origin_observers)pthread_cond_wait(&sst_origin_use_drained,&sst_origin_use_mutex);
    pthread_mutex_unlock(&sst_origin_use_mutex);
}
void sst_origin_lifecycle_end(void) {
    if(!sst_origin_mutation_depth)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);
    if(!sst_origin_mutators)abort();
    sst_origin_mutation_depth--;sst_origin_mutators--;
    pthread_mutex_unlock(&sst_origin_use_mutex);
}
static void origin_reset_locked(void) {
    atomic_store(&sst_origin_active_epoch,0);
    uint64_t old=atomic_load(&sst_origin_reset_serial);
    while(old!=UINT64_MAX && !atomic_compare_exchange_weak(&sst_origin_reset_serial,&old,old+1)){}
    if(old==UINT64_MAX)sst_origin_latch_unsupported();
    sst_origin_set_gpu_guard_ready(false);
    sst_origin_set_replay_requested(false);
    pthread_mutex_lock(&sst_origin_lease_mutex);
    memset(sst_origin_lease_table,0,sizeof sst_origin_lease_table);
    sst_origin_counters.live_leases=0;sst_origin_counters.resets++;
    pthread_mutex_unlock(&sst_origin_lease_mutex);
}
void sst_origin_reset(void) {
    pthread_mutex_lock(&sst_origin_use_mutex);origin_reset_locked();pthread_mutex_unlock(&sst_origin_use_mutex);
}
void sst_origin_latch_unsupported(void) {
    atomic_fetch_or(&sst_origin_reason_mask,SST_ORIGIN_STOP_UNSUPPORTED);
    atomic_store(&sst_origin_active_epoch,0);
    atomic_store(&sst_origin_stop_latch,true);atomic_store(&sst_origin_render_wanted,false);
}
uint32_t sst_origin_stop_reasons(void){return atomic_load(&sst_origin_reason_mask);}
bool sst_origin_hard_failed(void){return (sst_origin_stop_reasons()&SST_ORIGIN_STOP_UNSUPPORTED)!=0;}
bool sst_origin_unsupported(void) {
    if(atomic_load(&sst_origin_recovery_mode))return sst_origin_hard_failed() || !atomic_load(&sst_origin_active_epoch) || atomic_load(&sst_origin_gpu_retiring);
    return sst_origin_stop_reasons()!=0;
}
SstOriginStats sst_origin_stats(void) {
    pthread_mutex_lock(&sst_origin_lease_mutex);
    SstOriginStats out=sst_origin_counters;
    out.capacity=SST_ORIGIN_CAPACITY;out.slot_storage_bytes=sizeof sst_origin_lease_table;
    out.unsupported=sst_origin_unsupported();
    pthread_mutex_unlock(&sst_origin_lease_mutex);return out;
}

static bool origin_cohort_valid(const SstOriginCohort *c) {
    static const unsigned model[4]={0x540,0x5f8,0x648,0x7e0},callback[4]={0xa90,0xaa8,0x1f38,0x820};
    if(!c || !c->actor_address || c->actor_address>UINT64_MAX-0x2000 || !c->resource_file || c->family!=1 ||
       c->named_mask!=15 || c->corroborated_mask!=15 || c->callback_mask!=15 || c->complex_mask!=15)return false;
    for(unsigned i=0;i<4;i++) {
        if(c->models[i]!=c->actor_address+model[i] || c->callbacks[i]!=c->actor_address+callback[i] || !c->objects[i] || !c->resources[i])return false;
        for(unsigned j=0;j<i;j++)if(c->objects[i]==c->objects[j])return false;
    }
    return true;
}
static bool origin_cohort_same(const SstOriginCohort *a,const SstOriginCohort *b) {
    return origin_cohort_valid(a) && origin_cohort_valid(b) && a->actor_address==b->actor_address && a->actor_instance==b->actor_instance &&
        a->resource_file==b->resource_file && a->family==b->family && !memcmp(a->models,b->models,sizeof a->models) &&
        !memcmp(a->objects,b->objects,sizeof a->objects) && !memcmp(a->resources,b->resources,sizeof a->resources) && !memcmp(a->callbacks,b->callbacks,sizeof a->callbacks);
}
bool sst_origin_recovery_enable(void) {
    pthread_mutex_lock(&sst_origin_use_mutex);pthread_mutex_lock(&sst_origin_lease_mutex);
    bool ok=atomic_load(&sst_origin_recovery_mode) || (!sst_origin_last_ticket && !sst_origin_lifetime_armed() && !sst_origin_stop_reasons() &&
        !sst_origin_users && !sst_origin_mutators && !sst_origin_batches && !sst_origin_observers);
    if(ok)atomic_store(&sst_origin_recovery_mode,true);
    pthread_mutex_unlock(&sst_origin_lease_mutex);pthread_mutex_unlock(&sst_origin_use_mutex);return ok;
}
bool sst_origin_recovery_enabled(void){return atomic_load(&sst_origin_recovery_mode);}
uint64_t sst_origin_active_generation(void){return sst_origin_hard_failed()?0:atomic_load(&sst_origin_active_epoch);}
bool sst_origin_candidate_exists(void) {
    pthread_mutex_lock(&sst_origin_use_mutex);bool yes=sst_origin_candidate_valid && sst_origin_candidate_epoch==sst_origin_epoch_counter &&
        sst_origin_candidate_reset==atomic_load(&sst_origin_reset_serial);pthread_mutex_unlock(&sst_origin_use_mutex);return yes;
}
void sst_origin_model_begin(unsigned kind,uint64_t actor,SstOriginModelAttempt *out) {
    if(!out || kind>2 || !sst_origin_recovery_enabled() || sst_origin_use_kind || sst_origin_batch_active || sst_origin_observation_active || sst_origin_in_gpu_wait)abort();
    memset(out,0,sizeof *out);pthread_mutex_lock(&sst_origin_use_mutex);origin_owner_locked();
    if(sst_origin_mutators || sst_origin_epoch_counter==UINT64_MAX)sst_origin_latch_unsupported();
    if(sst_origin_epoch_counter!=UINT64_MAX)out->epoch=++sst_origin_epoch_counter;
    out->parent=sst_origin_model_top;out->actor=actor;out->kind=kind;sst_origin_model_top=out->epoch;
    if(sst_origin_mutators==UINT64_MAX || sst_origin_mutation_depth==UINT32_MAX)abort();
    sst_origin_mutators++;sst_origin_mutation_depth++;
    atomic_store(&sst_origin_active_epoch,0);sst_origin_candidate_valid=false;
    origin_close_known_lifecycle();origin_drain_locked();pthread_mutex_unlock(&sst_origin_use_mutex);
}
bool sst_origin_model_bind(SstOriginModelAttempt *a,uint32_t instance) {
    pthread_mutex_lock(&sst_origin_use_mutex);
    bool ok=a && a->epoch && a->epoch==sst_origin_model_top && a->epoch==sst_origin_epoch_counter && a->kind==0 &&
        !a->bound && sst_origin_mutators==1 && sst_origin_mutation_depth==1 && sst_origin_observation_active && !sst_origin_hard_failed();
    if(ok){a->instance=instance;a->bound=true;}
    pthread_mutex_unlock(&sst_origin_use_mutex);return ok;
}
bool sst_origin_model_complete(SstOriginModelAttempt *a,const SstOriginCohort *c) {
    pthread_mutex_lock(&sst_origin_use_mutex);
    bool ok=a && a->bound && !a->completed && a->epoch==sst_origin_model_top && a->epoch==sst_origin_epoch_counter &&
        sst_origin_mutators==1 && sst_origin_mutation_depth==1 && sst_origin_observation_active && !sst_origin_hard_failed() &&
        origin_cohort_valid(c) && a->actor==c->actor_address && a->instance==c->actor_instance;
    if(ok){sst_origin_candidate=*c;sst_origin_candidate_epoch=a->epoch;sst_origin_candidate_reset=atomic_load(&sst_origin_reset_serial);sst_origin_candidate_valid=true;a->completed=true;}
    else sst_origin_latch_unsupported();
    pthread_mutex_unlock(&sst_origin_use_mutex);return ok;
}
void sst_origin_model_end(SstOriginModelAttempt *a) {
    if(!a || (!a->epoch && !sst_origin_hard_failed()) || a->epoch!=sst_origin_model_top || sst_origin_observation_active || sst_origin_use_kind || sst_origin_batch_active)abort();
    pthread_mutex_lock(&sst_origin_use_mutex);
    if(!sst_origin_mutators || !sst_origin_mutation_depth)abort();
    if(a->kind==0 && !a->completed)sst_origin_latch_unsupported();
    sst_origin_model_top=a->parent;sst_origin_mutators--;sst_origin_mutation_depth--;memset(a,0,sizeof *a);
    pthread_mutex_unlock(&sst_origin_use_mutex);
}
bool sst_origin_activation_enter(SstOriginCohort *out) {
    if(out)memset(out,0,sizeof *out);pthread_mutex_lock(&sst_origin_use_mutex);origin_owner_locked();
    bool ok=out && sst_origin_recovery_enabled() && !sst_origin_hard_failed() && !sst_origin_active_generation() &&
        !sst_origin_mutators && !sst_origin_users && !sst_origin_batches && !sst_origin_observers && !atomic_load(&sst_origin_gpu_retiring) && sst_origin_candidate_valid &&
        sst_origin_candidate_epoch==sst_origin_epoch_counter && sst_origin_candidate_reset==atomic_load(&sst_origin_reset_serial);
    if(ok){*out=sst_origin_candidate;sst_origin_observers++;sst_origin_observation_active=sst_origin_activation_active=true;}
    pthread_mutex_unlock(&sst_origin_use_mutex);return ok;
}
bool sst_origin_activation_commit(const SstOriginCohort *c,const SstScene *scene,bool ready,bool noheld,uint8_t lifecycle) {
    pthread_mutex_lock(&sst_origin_use_mutex);
    bool scene_ok=scene && memchr(scene->stage,0,sizeof scene->stage) && scene->room==0 && scene->layer==1 &&
        (!strcmp(scene->stage,"F009r") || !strcmp(scene->stage,"F000"));
    bool ok=sst_origin_activation_active && sst_origin_observation_active && !sst_origin_mutators && !atomic_load(&sst_origin_gpu_retiring) && !sst_origin_hard_failed() &&
        sst_origin_candidate_valid && sst_origin_candidate_epoch==sst_origin_epoch_counter && sst_origin_candidate_reset==atomic_load(&sst_origin_reset_serial) &&
        ready && noheld && lifecycle==1 && scene_ok && origin_cohort_same(c,&sst_origin_candidate);
    if(ok) {
        pthread_mutex_lock(&sst_origin_lease_mutex);sst_origin_authority=*c;sst_origin_authority_scene=*scene;
        atomic_store(&sst_origin_active_epoch,sst_origin_candidate_epoch);pthread_mutex_unlock(&sst_origin_lease_mutex);
        if(sst_origin_hard_failed()){atomic_store(&sst_origin_active_epoch,0);ok=false;}
    }
    pthread_mutex_unlock(&sst_origin_use_mutex);return ok;
}
void sst_origin_activation_leave(void) {
    if(!sst_origin_activation_active)abort();sst_origin_activation_active=false;sst_origin_observation_leave();
}
uint64_t sst_origin_member_generation(const SstOriginIdentity *id) {
    if(!sst_origin_recovery_enabled())return 0;
    pthread_mutex_lock(&sst_origin_lease_mutex);SstOriginIdentity copy={0};uint64_t epoch=atomic_load(&sst_origin_active_epoch);
    if(id){copy=*id;copy.model_allocation_epoch=epoch;}
    bool ok=id && origin_authorized_identity(&copy);pthread_mutex_unlock(&sst_origin_lease_mutex);return ok?epoch:0;
}
bool sst_origin_active_scene_matches(const SstScene *scene) {
    pthread_mutex_lock(&sst_origin_lease_mutex);
    bool ok=scene && memchr(scene->stage,0,sizeof scene->stage) && sst_origin_active_generation() &&
        !strcmp(scene->stage,sst_origin_authority_scene.stage) && scene->room==sst_origin_authority_scene.room &&
        scene->layer==sst_origin_authority_scene.layer && scene->entrance==sst_origin_authority_scene.entrance;
    pthread_mutex_unlock(&sst_origin_lease_mutex);return ok;
}

static void origin_poison_scope(void) {
    sst_origin_tls_poisoned=true;sst_origin_tls_marker=sst_origin_forced_unresolved();
    sst_origin_latch_unsupported();
}
static bool origin_scope_cookie(uint64_t *out) {
    uint64_t previous=atomic_load(&sst_origin_scope_serial);
    while(previous!=UINT64_MAX) {
        if(atomic_compare_exchange_weak(&sst_origin_scope_serial,&previous,previous+1)) {
            *out=previous+1;return true;
        }
    }
    return false;
}
bool sst_origin_scope_push(SstOriginMarker marker,SstOriginScope *scope) {
    if(scope)memset(scope,0,sizeof *scope);
    uint64_t cookie;
    if(!scope || sst_origin_tls_poisoned || !origin_scope_cookie(&cookie)) {
        origin_poison_scope();return false;
    }
    scope->_previous=sst_origin_tls_marker;
    scope->_cookie=cookie;scope->_parent_cookie=sst_origin_tls_top;
    OriginDecoded raw=origin_decode(marker);
    if(raw.kind!=ORIGIN_NONE)sst_origin_tls_marker=origin_marker(raw.kind,raw.token);
    sst_origin_tls_top=cookie;return true;
}
bool sst_origin_scope_pop(SstOriginScope *scope) {
    if(!scope || sst_origin_tls_poisoned || !scope->_cookie || scope->_cookie!=sst_origin_tls_top) {
        origin_poison_scope();return false;
    }
    sst_origin_tls_marker=sst_origin_combine(scope->_previous,sst_origin_none());
    sst_origin_tls_top=scope->_parent_cookie;
    memset(scope,0,sizeof *scope);return true;
}
SstOriginMarker sst_origin_current(void) {return sst_origin_tls_marker;}
void sst_origin_scope_clear(void) {
    sst_origin_tls_marker=sst_origin_none();sst_origin_tls_top=0;sst_origin_tls_poisoned=false;
}

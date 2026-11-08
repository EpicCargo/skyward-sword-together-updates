// Experimental CPU-only command-origin metadata. No native/guest memory access.
#pragma once
#include "sst_net.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SST_ORIGIN_CAPACITY 8192u
#define SST_ORIGIN_MAX_AGE_MS UINT64_C(1000)
// Immutable startup opt-in for the scoped native feature. Diagnostic logging
// and research files are not prerequisites. SST_NO_PLAYER_READ disables it.
bool sst_origin_feature_enabled(void);

// Copy with memcpy. Never reinterpret these bytes as floats or pointers.
// Allzero is NONE. Malformed nonzero bytes are conservatively unresolved FORCE.
typedef struct { uint8_t bytes[16]; } SstOriginMarker;
typedef struct {
    uint64_t actor_address;
    uint32_t actor_instance; // zero is permitted; caller proves native identity
    SstScene scene; // stage/room/layer/entrance, compared semantically
    uint64_t model_address, object_address, resource_address;
    uint16_t role; // opaque caller-authorized component identity
    uint64_t model_registration_generation;
    uint64_t model_allocation_epoch; // independent verified cohort, zero in legacy mode
} SstOriginIdentity;
typedef struct {
    SstOriginIdentity identity;
    uint64_t recorded_ms;
} SstOriginProof;

SstOriginMarker sst_origin_none(void);
SstOriginMarker sst_origin_forced_unresolved(void);
// Explicit unowned barrier: blocks ONLY the same nonzero parent FORCE token.
// NONE/default barrier0 cannot clear a forced invocation; FORCE0 stays forced.
SstOriginMarker sst_origin_unowned(SstOriginMarker parent);
// Pure execution rule: intrinsic recorded FORCE wins; matching barrier returns
// NONE, clearing that forced invocation;
// empty record inherits invocation; unrelated/default barriers cannot clear FORCE.
SstOriginMarker sst_origin_combine(SstOriginMarker recorded, SstOriginMarker invocation);
bool sst_origin_is_forced(SstOriginMarker marker);
// Execution policy helpers: pass the result of combine(record, invocation), not
// an uncombined producer barrier/current(). Lease validity never changes these.
bool sst_origin_native_hidden(SstOriginMarker marker);
bool sst_origin_learning_allowed(SstOriginMarker marker);

// Deep copies caller-authorized metadata only, never dereferences its addresses.
// Proof requires nonzero actor/model/object/resource/registration-generation,
// nonempty bounded stage, and a nonfuture age<=1000ms. No native policy inferred.
// Every success issues a nonzero64bit token, NEVER reused across reset. Wrap fails
// closed. Failure writes FORCE0 when out!=NULL; caller MUST stop/latch experiment.
bool sst_origin_publish(const SstOriginProof *proof, uint64_t now_ms, SstOriginMarker *out);
// NONCONSUMING historical metadata read: marker/ticket/age checks ONLY. This is
// NOT native authorization. Addresses/role/generation are recorded values; engine
// must revalidate live ownership, full scene and resources before replay/peek.
// Failure zeros out. Source root detachment does not itself erase this metadata.
bool sst_origin_read(SstOriginMarker marker, uint64_t now_ms, SstOriginProof *out);
// Host-only authority/age check, BEFORE ANY native/payload read. No guest access.
bool sst_origin_authorize(SstOriginMarker marker,uint64_t now_ms,SstOriginProof *out);
// NONCONSUMING byvalue copy. Current identity must exactly match; timestamp is
// checked separately against the recorded proof. Failure zeros out when nonnull.
// Expiry/future time or observed metadata mismatch invalidates this lease. It
// NEVER clears marker FORCE/native-hidden/no-learning classification. Unsupported
// latch denies peek/replay; read remains a metadata-only diagnostic accessor.
bool sst_origin_peek(SstOriginMarker marker, const SstOriginIdentity *current,
                     uint64_t now_ms, SstOriginProof *out);
// Clears leases only: does NOT reset token sequence, TLS scopes, or unsupported
// latch. Previously recorded FORCE markers remain forced with unavailable replay.
void sst_origin_reset(void);
// Cross-thread render intent only; never changes historical record origin.
// Producer must stop adding sources while false. Existing forced records still
// remain native-hidden/non-learning. Reset returns intent to false.
void sst_origin_set_replay_requested(bool wanted);
bool sst_origin_replay_requested(void);

// CPU-use admission for the private source experiment. These functions hold NO
// mutex across caller work/native forwarding. Same-thread, nonrecursive pairing.
// Replay must not call guest code. Source may call only the audited registration
// routine; nested known lifetime mutation terminates rather than deadlocking.
// Neither gate is a GPU fence or a complete native allocation-lifetime proof.
bool sst_origin_replay_enter(void);
void sst_origin_replay_leave(void);
bool sst_origin_source_enter(void); // arm watch BEFORE source preflight/registration
void sst_origin_source_leave(void);
void sst_origin_recovery_owner_check(void); // fail-stop before native reads on a foreign owner
bool sst_origin_recovery_owner_current(void); // unrelated native worker callbacks remain unowned
bool sst_origin_emission_enter(void);
void sst_origin_emission_leave(void);
// Renderer-owned batch pin. Caller keeps it through submitting pending uploads
// and main commands AND strict actual completion of every admitted forced draw.
// CPU replay may nest inside a batch; backend batches themselves cannot nest.
// Deferred callers may instead transfer the pin with batch_submitted() below.
bool sst_origin_batch_enter(void);
void sst_origin_batch_leave(void);
// Optional submitted-ticket retirement. Caller callback must strictly acknowledge
// actual completion, never submit/flush, and return with no guest calls. The queue
// domain cannot change in this private mode. Legacy strict batches keep leave().
typedef bool (*SstOriginGpuWait)(void *context,uint64_t target,uint64_t *completed);
typedef struct {uint64_t domain,pending,completed,registered_batches,wait_calls,wait_completions,failures,last_wait_target;} SstOriginGpuRetireStats;
bool sst_origin_gpu_retirement_configure(uint64_t domain,SstOriginGpuWait wait,void *context);
void sst_origin_batch_submitted(uint64_t actual_submitted); // publishes ticket AND releases pin atomically
SstOriginGpuRetireStats sst_origin_gpu_retirement_status(void);
void sst_origin_set_gpu_guard_ready(bool ready);
bool sst_origin_gpu_guard_ready(void);
// Diagnostic snapshot inside exactly one current-thread known mutator. This
// does not authorize replay or reopen STOP. Other known mutations wait for it;
// overlapping/nested mutators reject observation before any native field read.
bool sst_origin_observation_enter(void);
void sst_origin_observation_leave(void);
// Watch is persistent across reset. Arming during a known mutation stops the
// experiment, closing the pre-first-publication race. Publish also arms the watch.
bool sst_origin_lifetime_arm(void);
bool sst_origin_lifetime_armed(void);
// Known lifecycle boundaries bracket original native mutation, even before any
// source exists. Begin closes admission/drains CPU users when armed; end marks
// mutation complete. No locks remain held on return; nesting is supported.
void sst_origin_lifecycle_begin(void);
void sst_origin_lifecycle_end(void);
// Permanently close admission, drain CPU users and clear optional leases.
// Never call from an admitted use. Reset cannot reopen or unforce old records.
void sst_origin_revoke(void);

// Scope receipts are caller-owned, byvalue and LIFO on the SAME host thread.
// Treat fields as opaque: do not copy/reuse/modify receipts or let their caller
// frame end before pop. No proof/receipt pointer is retained by the module.
// Producer push preserves RAW FORCE/BARRIER for recording; NONE inherits. This
// deliberately differs from execution combine: erasing a matching barrier while
// recording would lose its blocked-parent token on cached-list execution.
// Explicit unowned must carry the actual parent token. Always pop a successful
// push after original native return. A failed/out-of-order scope poisons current scope as FORCE0 and
// latches unsupported; originals must still forward unchanged. Nonlocal unwinds
// require scope_clear ONLY at a known safe boundary, never during live scopes.
typedef struct {
    SstOriginMarker _previous;
    uint64_t _cookie, _parent_cookie;
} SstOriginScope;
bool sst_origin_scope_push(SstOriginMarker marker, SstOriginScope *scope);
bool sst_origin_scope_pop(SstOriginScope *scope);
SstOriginMarker sst_origin_current(void);
void sst_origin_scope_clear(void); // current thread only; safe outside callbacks

// Monotone reasons, never cleared by reset, constructor observation or scopes.
// A known boundary is a reason for closure, NOT permission to recover. Legacy
// unsupported() means ANY closure in legacy mode; scoped mode also checks authority.
enum {SST_ORIGIN_STOP_KNOWN_LIFECYCLE=1u,SST_ORIGIN_STOP_UNSUPPORTED=2u};
uint32_t sst_origin_stop_reasons(void);
bool sst_origin_hard_failed(void);
// Sticky hard-failure reason. Does not unforce recorded commands.
void sst_origin_latch_unsupported(void);
bool sst_origin_unsupported(void);
typedef struct {
    uint32_t capacity, live_leases; // occupied slots; age invalidation is lazy
    size_t slot_storage_bytes;
    uint64_t published, publish_failures, reads, read_failures, peeks, peek_failures;
    uint64_t expired, overwritten, resets;
    bool unsupported;
} SstOriginStats;
SstOriginStats sst_origin_stats(void);

// PRIVATE opt-in scoped recovery. Historical reason bits remain monotone; only
// verified active authority can satisfy a known closure. Hard failure never can.
typedef struct {
    uint64_t actor_address,resource_file;
    uint32_t actor_instance;uint16_t family;
    uint64_t models[4],objects[4],resources[4],callbacks[4];
    uint32_t named_mask,corroborated_mask,callback_mask,complex_mask,metadata_bytes;
} SstOriginCohort;
typedef struct {
    uint64_t epoch,parent,actor;uint32_t instance,kind;
    bool bound,completed;
} SstOriginModelAttempt; // opaque same-thread/LIFO receipt, never edit or copy
bool sst_origin_recovery_enable(void); // only before any watch/ticket/thread use
bool sst_origin_recovery_enabled(void);
uint64_t sst_origin_active_generation(void);
bool sst_origin_candidate_exists(void);
void sst_origin_model_begin(unsigned kind,uint64_t actor,SstOriginModelAttempt *out);
bool sst_origin_model_bind(SstOriginModelAttempt *attempt,uint32_t instance);
bool sst_origin_model_complete(SstOriginModelAttempt *attempt,const SstOriginCohort *cohort);
void sst_origin_model_end(SstOriginModelAttempt *attempt);
bool sst_origin_activation_enter(SstOriginCohort *candidate);
bool sst_origin_activation_commit(const SstOriginCohort *current,const SstScene *scene,
                                  bool gameplay_ready,bool no_held,uint8_t lifecycle);
void sst_origin_activation_leave(void);
// Returns current epoch only for an exactly bound active role + full scene.
uint64_t sst_origin_member_generation(const SstOriginIdentity *member);
bool sst_origin_active_scene_matches(const SstScene *scene);

// Caller owns authority/lifetimes: native owner+scene/model epochs, current pass,
// command/list reuse and unsupported CALL/COPY/indirect/etc policy. This module
// does not authorize drawing, reconstruct poses/cameras, or solve GPU provenance.
#ifdef __cplusplus
}
#endif

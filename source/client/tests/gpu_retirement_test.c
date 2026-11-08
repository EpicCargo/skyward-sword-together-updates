// Actual deferred-retirement service. Fake GPU acknowledges explicit tickets;
// native tests separately prove the Vulkan callback and ordering.
#include "../src/sst_origin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SstOriginCohort cohort(void) {
 SstOriginCohort c={.actor_address=0x100000,.actor_instance=42,.resource_file=0x400000,.family=1,.named_mask=15,.corroborated_mask=15,.callback_mask=15,.complex_mask=15};
 const unsigned m[4]={0x540,0x5f8,0x648,0x7e0},b[4]={0xa90,0xaa8,0x1f38,0x820};
 for(unsigned i=0;i<4;i++){c.models[i]=c.actor_address+m[i];c.callbacks[i]=c.actor_address+b[i];c.objects[i]=0x300000+i*0x1000;c.resources[i]=0x400100+i*0x1000;}return c;
}
static void construct_activate(void) {
 SstOriginCohort c=cohort();SstOriginModelAttempt a;sst_origin_model_begin(0,c.actor_address,&a);
 assert(sst_origin_observation_enter());assert(sst_origin_model_bind(&a,c.actor_instance));assert(sst_origin_model_complete(&a,&c));sst_origin_observation_leave();sst_origin_model_end(&a);
 SstOriginCohort copy;assert(sst_origin_activation_enter(&copy));SstScene scene={.stage="F009r",.room=0,.layer=1};assert(sst_origin_activation_commit(&c,&scene,true,true,1));sst_origin_activation_leave();
}
typedef struct {unsigned calls;uint64_t last;bool fail,short_ack,hard_during;} Fake;
static bool wait_gpu(void *context,uint64_t ticket,uint64_t *completed) {
 Fake *f=context;f->calls++;f->last=ticket;
 // These APIs acquire use_mutex: callback must be outside that lock. Known
 // mutator/closed authority remains live and cannot admit source or activation.
 SstOriginGpuRetireStats s=sst_origin_gpu_retirement_status();assert(s.last_wait_target==ticket && !sst_origin_active_generation());
 SstOriginCohort candidate;assert(!sst_origin_activation_enter(&candidate));assert(!sst_origin_batch_enter());
 if(f->hard_during)sst_origin_latch_unsupported();
 *completed=f->short_ack?ticket-1:ticket;return !f->fail;
}
static void submit(uint64_t ticket) {assert(sst_origin_batch_enter());assert(sst_origin_replay_enter());sst_origin_replay_leave();sst_origin_batch_submitted(ticket);}
int main(int argc,char **argv) {
 assert(argc==2 && sst_origin_recovery_enable());Fake gpu={0};construct_activate();
 assert(sst_origin_gpu_retirement_configure(1,wait_gpu,&gpu));assert(sst_origin_gpu_retirement_configure(1,wait_gpu,&gpu));assert(!sst_origin_gpu_retirement_configure(2,wait_gpu,&gpu));
 submit(7);submit(11);assert(!gpu.calls);SstOriginGpuRetireStats s=sst_origin_gpu_retirement_status();assert(s.pending==11 && s.completed==0 && s.registered_batches==2);
 if(!strcmp(argv[1],"failure"))gpu.fail=true;else if(!strcmp(argv[1],"short"))gpu.short_ack=true;else if(!strcmp(argv[1],"hard_wait"))gpu.hard_during=true;else assert(!strcmp(argv[1],"normal") || !strcmp(argv[1],"closed_submit"));
 if(!strcmp(argv[1],"closed_submit")){assert(sst_origin_batch_enter());sst_origin_latch_unsupported();sst_origin_batch_submitted(19);assert(sst_origin_gpu_retirement_status().pending==19);}
 // Successful return is the point at which the caller could enter native free.
 SstOriginModelAttempt d;sst_origin_model_begin(2,cohort().actor_address,&d);
 assert(gpu.calls==1 && gpu.last==(!strcmp(argv[1],"closed_submit")?19:11));
 s=sst_origin_gpu_retirement_status();assert(s.completed>=s.pending && s.wait_calls==1 && s.wait_completions==1 && !s.failures);sst_origin_model_end(&d);
 if(!strcmp(argv[1],"normal")) {
  construct_activate();assert(gpu.calls==1);submit(23);sst_origin_reset();assert(sst_origin_gpu_retirement_status().pending==23);
  SstOriginCohort c=cohort();SstOriginModelAttempt a;sst_origin_model_begin(0,c.actor_address,&a);assert(gpu.calls==2 && gpu.last==23);
  assert(sst_origin_observation_enter());assert(sst_origin_model_bind(&a,c.actor_instance));assert(sst_origin_model_complete(&a,&c));sst_origin_observation_leave();sst_origin_model_end(&a);
 } else assert(sst_origin_hard_failed() && !sst_origin_active_generation());
 puts("deferred retirement PASS: no per-batch wait, max submitted ticket before pin release, unlocked strict drain before mutation, reset/hard persistence");return 0;
}

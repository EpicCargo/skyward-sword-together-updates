// Actual reason/admission API regressions. Fresh process per monotone scenario.
#include "../src/sst_origin.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static SstOriginProof proof(void) {return (SstOriginProof){.identity={.actor_address=1,.model_address=2,.object_address=3,.resource_address=4,.model_registration_generation=1,.scene={.stage="TEST"}},.recorded_ms=100};}
static void known(void){sst_origin_lifecycle_begin();sst_origin_lifecycle_end();}
static void *known_worker(void *unused){(void)unused;for(unsigned i=0;i<64;i++)known();return NULL;}
static void *hard_worker(void *unused){(void)unused;for(unsigned i=0;i<4096;i++)sst_origin_latch_unsupported();return NULL;}
int main(int argc,char **argv) {
 assert(argc==2);assert(!sst_origin_stop_reasons() && !sst_origin_hard_failed());
 SstOriginProof p=proof(),out;SstOriginMarker mark;
 unsigned want=0;
 if(!strcmp(argv[1],"arm_overlap") || !strcmp(argv[1],"source_overlap")) {
  sst_origin_lifecycle_begin();assert(!sst_origin_stop_reasons());
  if(!strcmp(argv[1],"arm_overlap"))assert(!sst_origin_lifetime_arm());else assert(!sst_origin_source_enter());
  sst_origin_lifecycle_end();mark=sst_origin_forced_unresolved();want=SST_ORIGIN_STOP_KNOWN_LIFECYCLE;
 } else {
  assert(sst_origin_publish(&p,100,&mark));
  if(!strcmp(argv[1],"known")){known();want=1;}
  else if(!strcmp(argv[1],"hard")){sst_origin_revoke();want=2;}
  else if(!strcmp(argv[1],"known_hard")){known();sst_origin_latch_unsupported();want=3;}
  else if(!strcmp(argv[1],"hard_known")){sst_origin_latch_unsupported();known();want=3;}
  else if(!strcmp(argv[1],"race")) {
   pthread_t a,b;assert(!pthread_create(&a,NULL,known_worker,NULL));assert(!pthread_create(&b,NULL,hard_worker,NULL));
   assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));want=3;
  } else if(!strcmp(argv[1],"poison")) {
   known();SstOriginScope scope={0};assert(!sst_origin_scope_pop(&scope));sst_origin_scope_clear();want=3;
  } else {assert(0);}
 }
 assert(sst_origin_stop_reasons()==want && sst_origin_unsupported());
 assert(sst_origin_hard_failed()==!!(want&SST_ORIGIN_STOP_UNSUPPORTED));
 sst_origin_reset();sst_origin_scope_clear();sst_origin_set_replay_requested(true);sst_origin_set_gpu_guard_ready(true);
 assert(sst_origin_stop_reasons()==want && !sst_origin_replay_requested() && !sst_origin_gpu_guard_ready());
 assert(!sst_origin_source_enter() && !sst_origin_replay_enter() && !sst_origin_batch_enter());
 assert(!sst_origin_peek(mark,&p.identity,100,&out));assert(sst_origin_native_hidden(mark) && !sst_origin_learning_allowed(mark));
 SstOriginMarker denied;assert(!sst_origin_publish(&p,100,&denied));assert(sst_origin_stop_reasons()==want);
 printf("reason %s PASS: mask%u, all legacy admission closed, immutable FORCE, hard failure cannot be downgraded\n",argv[1],want);
 return 0;
}

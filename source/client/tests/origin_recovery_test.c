// Actual scoped authority service + shared consumer guard. No game memory.
#include "../src/sst_origin.h"
#include "../../engine/runtime/nvn/source_policy.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
static SstOriginCohort cohort(void) {
 SstOriginCohort c={.actor_address=0x100000,.actor_instance=42,.resource_file=0x400000,.family=1,.named_mask=15,.corroborated_mask=15,.callback_mask=15,.complex_mask=15};
 const unsigned m[4]={0x540,0x5f8,0x648,0x7e0},cb[4]={0xa90,0xaa8,0x1f38,0x820};
 for(unsigned i=0;i<4;i++){c.models[i]=c.actor_address+m[i];c.callbacks[i]=c.actor_address+cb[i];c.objects[i]=0x300000+i*0x1000;c.resources[i]=0x400100+i*0x1000;}
 return c;
}
static SstScene scene={.stage="F009r",.room=0,.layer=1,.entrance=0};
static uint64_t construct(SstOriginCohort c) {
 SstOriginModelAttempt a;sst_origin_model_begin(0,c.actor_address,&a);uint64_t e=a.epoch;
 assert(sst_origin_observation_enter());assert(sst_origin_model_bind(&a,c.actor_instance));assert(sst_origin_model_complete(&a,&c));sst_origin_observation_leave();sst_origin_model_end(&a);
 assert(sst_origin_unsupported() && !sst_origin_active_generation());return e;
}
static bool activate(SstOriginCohort c) {
 SstOriginCohort candidate;assert(sst_origin_activation_enter(&candidate));
 bool ok=sst_origin_activation_commit(&c,&scene,true,true,1);sst_origin_activation_leave();return ok;
}
static SstOriginProof proof(SstOriginCohort c,uint64_t epoch) {
 return (SstOriginProof){.identity={.actor_address=c.actor_address,.actor_instance=c.actor_instance,.scene=scene,.model_address=c.models[0],.object_address=c.objects[0],.resource_address=c.resources[0],.role=0,.model_registration_generation=1,.model_allocation_epoch=epoch},.recorded_ms=100};
}
static void *resetter(void *p){(void)p;sst_origin_reset();return NULL;}
static void *emitter(void *arg) {
 SstOriginProof *p=arg;assert(!sst_origin_recovery_owner_current());assert(sst_origin_emission_enter());
 SstOriginMarker mark;assert(sst_origin_publish(p,100,&mark));sst_origin_emission_leave();return NULL;
}
static _Atomic bool drain_admitted,drain_post_reads;
static void *draining_emitter(void *arg) {
 SstOriginProof *p=arg;assert(sst_origin_emission_enter());SstOriginMarker mark;assert(sst_origin_publish(p,100,&mark));atomic_store(&drain_admitted,true);
 const struct timespec tick={.tv_nsec=1000000};for(unsigned i=0;i<3000 && sst_origin_active_generation();i++)nanosleep(&tick,NULL);
 assert(!sst_origin_active_generation());assert(!sst_origin_publish(p,100,&mark));
 atomic_store(&drain_post_reads,true);sst_origin_emission_leave();return NULL;
}
static void *hard(void *p){(void)p;sst_origin_latch_unsupported();return NULL;}
int main(int argc,char **argv) {
 assert(argc==2 && sst_origin_recovery_enable());SstOriginCohort c=cohort();
 if(!strcmp(argv[1],"invalid")) {
  SstOriginModelAttempt a;sst_origin_model_begin(0,c.actor_address,&a);assert(sst_origin_observation_enter());assert(sst_origin_model_bind(&a,c.actor_instance));c.named_mask=7;assert(!sst_origin_model_complete(&a,&c));sst_origin_observation_leave();sst_origin_model_end(&a);
  assert(sst_origin_hard_failed() && !sst_origin_candidate_exists());
 } else if(!strcmp(argv[1],"nested")) {
  SstOriginModelAttempt a,b;sst_origin_model_begin(0,c.actor_address,&a);sst_origin_model_begin(2,c.actor_address,&b);sst_origin_model_end(&b);
  assert(sst_origin_observation_enter());assert(!sst_origin_model_bind(&a,c.actor_instance));sst_origin_observation_leave();sst_origin_model_end(&a);assert(sst_origin_hard_failed());
 } else {
  uint64_t first=construct(c);assert(sst_origin_candidate_exists());
  if(!strcmp(argv[1],"context")) {
   SstOriginCohort candidate;assert(sst_origin_activation_enter(&candidate));
   assert(!sst_origin_activation_commit(&c,&scene,true,true,0));assert(!sst_origin_activation_commit(&c,&scene,false,true,1));assert(!sst_origin_activation_commit(&c,&scene,true,false,1));
   SstScene bad=scene;strcpy(bad.stage,"F999");assert(!sst_origin_activation_commit(&c,&bad,true,true,1));
   SstOriginCohort mixed=c;mixed.callbacks[2]++;assert(!sst_origin_activation_commit(&mixed,&scene,true,true,1));
   assert(!sst_origin_active_generation());assert(sst_origin_activation_commit(&c,&scene,true,true,1));sst_origin_activation_leave();
  } else if(!strcmp(argv[1],"reset")) {
   sst_origin_reset();assert(!sst_origin_candidate_exists());SstOriginCohort candidate;assert(!sst_origin_activation_enter(&candidate));
   assert(construct(c)>first && activate(c));
  } else if(!strcmp(argv[1],"reset_race")) {
   SstOriginCohort candidate;assert(sst_origin_activation_enter(&candidate));pthread_t t;assert(!pthread_create(&t,NULL,resetter,NULL));
   sst_origin_activation_commit(&c,&scene,true,true,1);assert(!pthread_join(t,NULL));sst_origin_activation_leave();
   assert(!sst_origin_active_generation() && !sst_origin_candidate_exists());
  } else if(!strcmp(argv[1],"hard") || !strcmp(argv[1],"hard_race")) {
   SstOriginCohort candidate;assert(sst_origin_activation_enter(&candidate));pthread_t t;
   if(!strcmp(argv[1],"hard")){assert(!pthread_create(&t,NULL,hard,NULL));assert(!pthread_join(t,NULL));assert(!sst_origin_activation_commit(&c,&scene,true,true,1));}
   else {assert(!pthread_create(&t,NULL,hard,NULL));sst_origin_activation_commit(&c,&scene,true,true,1);assert(!pthread_join(t,NULL));}
   sst_origin_activation_leave();assert(sst_origin_hard_failed() && !sst_origin_active_generation());
  } else if(!strcmp(argv[1],"worker_drain")) {
   assert(activate(c));SstOriginProof p=proof(c,first);pthread_t t;assert(!pthread_create(&t,NULL,draining_emitter,&p));
   const struct timespec tick={.tv_nsec=1000000};for(unsigned i=0;i<3000 && !atomic_load(&drain_admitted);i++)nanosleep(&tick,NULL);assert(atomic_load(&drain_admitted));
   SstOriginModelAttempt d;sst_origin_model_begin(2,c.actor_address,&d);assert(atomic_load(&drain_post_reads));sst_origin_model_end(&d);assert(!pthread_join(t,NULL));assert(!sst_origin_hard_failed());
  } else if(!strcmp(argv[1],"worker")) {
   assert(activate(c));SstOriginProof p=proof(c,first);pthread_t t;assert(!pthread_create(&t,NULL,emitter,&p));assert(!pthread_join(t,NULL));
   assert(!sst_origin_hard_failed() && sst_origin_active_generation()==first);
  } else {
   assert(!strcmp(argv[1],"cycle"));assert(activate(c));assert(sst_origin_stop_reasons()==1 && !sst_origin_unsupported());
   SstOriginProof p=proof(c,first),out;SstOriginMarker old;assert(sst_origin_publish(&p,100,&old));assert(sst_origin_authorize(old,100,&out));
   SstOriginModelAttempt d;sst_origin_model_begin(2,c.actor_address,&d);sst_origin_model_end(&d);assert(!sst_origin_authorize(old,100,&out));
   uint64_t fresh=construct(c);assert(fresh>first);assert(activate(c)); // SAME addresses AND SAME native instance
   assert(!sst_origin_authorize(old,100,&out));assert(sst_origin_native_hidden(old) && !sst_origin_learning_allowed(old));
   CmdRec stale={.op=CMD_SET_RENDER_TARGETS,.size=sizeof(CmdRec)};memcpy(stale.source_origin,old.bytes,16);stale.x[0]=1;
   SstOriginMarker effective;assert(!nvn_source_command_allowed(&stale,sst_origin_none(),&effective,100));
   assert(!sst_origin_publish(&p,100,&old));p=proof(c,fresh);SstOriginMarker next;assert(sst_origin_publish(&p,100,&next));
   assert(sst_origin_authorize(next,100,&out));memcpy(stale.source_origin,next.bytes,16);assert(nvn_source_command_allowed(&stale,sst_origin_none(),&effective,100));
   assert(!nvn_source_command_allowed(&stale,sst_origin_none(),&effective,1101));
  }
 }
 if(sst_origin_hard_failed()){sst_origin_reset();assert(sst_origin_unsupported() && !sst_origin_active_generation());}
 printf("scoped recovery %s PASS: independent receipt/current authority, closed until ACTIVE validation, stale binds denied and hard failure dominates\n",argv[1]);return 0;
}

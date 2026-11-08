#include "../src/sst_world.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SstEnemies s;
static uint8_t flags[8192];
static SstEnemyEvent last;
static unsigned sent;
static void capture(const SstEnemyEvent *e,void *u) {(void)u;last=*e;sent++;}
static void bit(unsigned id,bool value) {
    unsigned byte=(id/16)*2+(id%16)/8, mask=1u<<(id%8);
    if(value) flags[byte]|=mask; else flags[byte]&=(uint8_t)~mask;
}
static void poll(void) {sst_enemies_defeat_flags(&s,flags,sizeof flags,capture,NULL);}
int main(void) {
    SstScene scene={.stage="D200"};unsigned id=12;
    uint64_t key=sst_world_enemy_key(0x17e,id,0x1234);
    sst_enemies_begin(&s,&scene,1);
    sst_enemies_observe_live(&s,key,10,0x17e,12,capture,NULL);poll();
    unsigned before=sent;uint32_t sequence=s.sequence;
    sst_enemies_begin(&s,&scene,1);poll();assert(sent==before);
    bit(id,true);poll();assert(sent==before+1&&last.snapshot&&!last.health&&!last.damage&&last.sequence>sequence);
    poll();assert(sent==before+1);
    SstEnemyEvent probe={.from=2,.scene=scene,.key=key,.profile=0x17e,.health=12,.snapshot=true,.sequence=1,.presence_generation=1};
    assert(!sst_enemies_exchange(&s,&probe,capture,NULL));assert(sent==before+2&&!last.health);
    // A bit already set on discovery is not proof about this instance.
    sst_enemies_begin(&s,&scene,2);sst_enemies_observe_live(&s,key,20,0x17e,12,capture,NULL);poll();
    before=sent;sst_enemies_begin(&s,&scene,2);poll();assert(sent==before);
    sst_enemies_observe_live(&s,key,20,0x17e,12,capture,NULL);bit(id,false);poll();
    bit(id,true);poll();assert(sent==before); // still present
    sst_enemies_begin(&s,&scene,2);poll();assert(sent==before+1&&!last.health);
    // Placement reuse cannot inherit its old clear-bit proof.
    sst_enemies_observe_live(&s,key,21,0x17e,12,capture,NULL);poll();before=sent;
    sst_enemies_begin(&s,&scene,2);poll();assert(sent==before);
    // Provisional zero cannot establish a positive baseline.
    bit(id,false);sst_enemies_begin(&s,&scene,3);
    sst_enemies_observe_live(&s,key,30,0x17e,0,capture,NULL);poll();before=sent;
    bit(id,true);sst_enemies_begin(&s,&scene,3);poll();assert(sent==before);
    sst_enemies_observe_live(&s,key,30,0x17e,12,capture,NULL);bit(id,false);poll();
    before=sent;bit(id,true);sst_enemies_begin(&s,&scene,3);poll();assert(sent==before+1);
    // Invalid tables cannot establish evidence; visits forget old evidence.
    bit(id,false);sst_enemies_begin(&s,&scene,4);sst_enemies_observe_live(&s,key,40,0x17e,12,capture,NULL);
    sst_enemies_defeat_flags(&s,NULL,sizeof flags,capture,NULL);
    sst_enemies_defeat_flags(&s,flags,sizeof flags-1,capture,NULL);
    sst_enemies_defeat_flags(&s,flags,sizeof flags,NULL,NULL);
    before=sent;bit(id,true);sst_enemies_begin(&s,&scene,4);poll();assert(sent==before);
    bit(id,false);sst_enemies_observe_live(&s,key,40,0x17e,12,capture,NULL);poll();
    before=sent;bit(id,true);sst_enemies_begin(&s,&scene,5);poll();assert(sent==before);
    // Observed and remote defeats do not produce flag echoes.
    bit(id,false);sst_enemies_observe_live(&s,key,50,0x17e,12,capture,NULL);poll();
    sst_enemies_observe_live(&s,key,50,0x17e,0,capture,NULL);before=sent;
    bit(id,true);sst_enemies_begin(&s,&scene,5);poll();assert(sent==before);
    bit(id,false);sst_enemies_observe_live(&s,key,51,0x17e,12,capture,NULL);poll();
    probe.health=0;probe.sequence=2;assert(sst_enemies_exchange(&s,&probe,capture,NULL));
    before=sent;bit(id,true);sst_enemies_begin(&s,&scene,5);poll();assert(sent==before);
    // Boss/trial/title stages and unplaced keys cannot confirm a defeat.
    const char *excluded[]={"B100","S100","Title"};
    for(unsigned i=0;i<3;i++) {
        strcpy(scene.stage,excluded[i]);sst_enemies_begin(&s,&scene,6+i);bit(id,false);
        sst_enemies_observe(&s,key,60+i,0x17e,12,capture,NULL);poll();before=sent;
        bit(id,true);sst_enemies_begin(&s,&scene,6+i);poll();assert(sent==before);
    }
    strcpy(scene.stage,"D200");sst_enemies_begin(&s,&scene,10);memset(flags,0,sizeof flags);
    uint64_t unplaced=sst_world_enemy_key(0x17e,65535,0);
    sst_enemies_observe(&s,unplaced,100,0x17e,12,capture,NULL);poll();before=sent;
    bit(65535,true);sst_enemies_begin(&s,&scene,10);poll();assert(sent==before);
    // Full capacity and the top valid native bit stay bounded and confirm once.
    sst_enemies_begin(&s,&scene,11);memset(flags,0,sizeof flags);
    for(unsigned i=0;i<SST_ENEMIES;i++) {
        unsigned ident=i==SST_ENEMIES-1?65534:i;
        sst_enemies_observe_live(&s,sst_world_enemy_key(0x16d,ident,0),i+1,0x16d,4,capture,NULL);
    }
    poll();before=sent;
    for(unsigned i=0;i<SST_ENEMIES;i++)bit(i==SST_ENEMIES-1?65534:i,true);
    sst_enemies_begin(&s,&scene,11);poll();assert(sent==before+SST_ENEMIES&&!last.health&&last.snapshot&&!last.damage);
    before=sent;poll();assert(sent==before);
    printf("Native defeat flags PASS: missed zero, stale flags, instances, visits, provisional health, exclusions, retained replies and256 actors; state=%zu bytes\n",sizeof s);
}

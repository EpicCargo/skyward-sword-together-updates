#include "../src/sst_world.h"
#include <assert.h>
#include <stdio.h>
static unsigned replies, births, hits;static SstEnemyEvent last;
static void capture(const SstEnemyEvent *e,void *unused){(void)unused;last=*e;if(e->snapshot)births++;else hits++;replies++;}
int main(void){
 static SstEnemies s;SstScene scene={.stage="D000"};uint64_t key=sst_world_enemy_key(0x16d,7,0x1234);
 sst_enemies_begin(&s,&scene,1);sst_enemies_observe_live(&s,key,1,0x16d,3,capture,NULL);assert(replies==1&&births==1);replies=0;
 SstEnemyEvent e={.scene=scene,.key=key,.profile=0x16d,.health=4,.snapshot=true,.presence_generation=1};
 // Every nonzero u16 sender can obtain a reply, even after all earlier IDs were used.
 for(unsigned id=1;id<=65535;id++){e.from=(uint16_t)id;e.sequence=0;assert(!sst_enemies_exchange(&s,&e,capture,NULL));}
 assert(replies==65535);
 // Remember all of their duplicate guards, rather than evicting old senders.
 for(unsigned id=1;id<=65535;id++){e.from=(uint16_t)id;assert(!sst_enemies_exchange(&s,&e,capture,NULL));}
 assert(replies==65535);e.from=0;assert(!sst_enemies_exchange(&s,&e,capture,NULL));assert(replies==65535);
 e.from=65535;e.presence_generation=2;e.sequence=0;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==65536);
 e.presence_generation=1;e.sequence=0xffffffff;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==65536);
 e.from=42;e.presence_generation=2;e.sequence=0xfffffffe;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==65537);
 e.sequence=0;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==65538);sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==65538);
 e.sequence=0xfffffffd;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==65538);
 // A new visit resets source guards but preserves this client's outgoing sequence.
 uint32_t outgoing=s.sequence;sst_enemies_begin(&s,&scene,2);assert(s.sequence==outgoing);
 sst_enemies_observe_live(&s,key,2,0x16d,3,capture,NULL);e.from=1;e.presence_generation=1;e.sequence=0;unsigned before=replies;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==before+1);
 // A first observed zero neither announces nor answers probes as a defeat.
 sst_enemies_begin(&s,&scene,3);replies=births=hits=0;
 for(unsigned i=0;i<30;i++)sst_enemies_observe_live(&s,key,3,0x16d,0,capture,NULL);
 sst_enemies_announce(&s,capture,NULL);assert(!replies);
 e.from=1;e.sequence=1;e.health=4;assert(!sst_enemies_exchange(&s,&e,capture,NULL));assert(!replies);
 e.sequence=2;e.health=0;assert(!sst_enemies_exchange(&s,&e,capture,NULL));assert(!replies);
 // Becoming ready emits one positive baseline, without inventing damage or refills.
 sst_enemies_observe_live(&s,key,3,0x16d,4,capture,NULL);assert(replies==1&&births==1&&!hits&&last.health==4&&!last.damage);
 sst_enemies_observe_live(&s,key,3,0x16d,4,capture,NULL);assert(replies==1);
 // Once established, a genuine zero still propagates and remains available after removal.
 sst_enemies_observe_live(&s,key,3,0x16d,0,capture,NULL);assert(replies==2&&hits==1&&last.damage==4&&!last.health);
 sst_enemies_begin(&s,&scene,3);e.sequence=3;e.health=4;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==3&&last.snapshot&&!last.health);
 // Reusing the key for another native instance starts a new provisional baseline.
 sst_enemies_observe_live(&s,key,4,0x16d,0,capture,NULL);before=replies;sst_enemies_announce(&s,capture,NULL);assert(replies==before);
 e.sequence=4;sst_enemies_exchange(&s,&e,capture,NULL);assert(replies==before);
 sst_enemies_observe_live(&s,key,4,0x16d,4,capture,NULL);assert(replies==before+1&&last.health==4);
 e.sequence=5;e.health=0;SstEnemyTrack *t=sst_enemies_exchange(&s,&e,capture,NULL);assert(t&&t->health==0&&!t->provisional);
 printf("Enemy stream guards PASS: all65535 sender IDs, retained duplicates, reconnect generation, sequence wrap, provisional zero and native readiness; state=%zu bytes\n",sizeof s);
}

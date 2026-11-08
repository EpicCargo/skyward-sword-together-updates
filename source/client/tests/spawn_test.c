#include "../src/sst_world.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static SstEnemyEvent sent[2048];static unsigned n;
static void capture(const SstEnemyEvent *e,void *unused){(void)unused;assert(n<2048);sent[n++]=*e;}
static SstEnemyEvent wire(SstEnemyEvent e,uint16_t from){e.from=from;e.presence_generation=1;return e;}
int main(void){
 SstScene scene={.stage="F006r",.layer=1};uint64_t key=sst_world_enemy_key(0x16d,7,0x1234);
 static SstEnemies a={0},b={0};sst_enemies_begin(&a,&scene,1);sst_enemies_begin(&b,&scene,2);
 sst_enemies_observe_live(&a,key,10,0x16d,4,capture,NULL);assert(n==1&&sent[0].snapshot&&!sent[0].damage&&sent[0].health==4);
 for(unsigned i=0;i<30;i++){sst_enemies_observe_live(&a,key,10,0x16d,4,capture,NULL);}assert(n==1);
 // A snapshot heard before an actor exists is recovered by the later birth exchange.
 SstEnemyEvent old={.scene=scene,.key=key,.profile=0x16d,.from=1,.sequence=1,.health=3,.snapshot=true,.presence_generation=1};
 assert(!sst_enemies_exchange(&b,&old,capture,NULL));
 sst_enemies_observe_live(&a,key,10,0x16d,3,capture,NULL);assert(n==2&&!sent[1].snapshot&&sent[1].damage==1);
 sst_enemies_observe_live(&b,key,20,0x16d,4,capture,NULL);SstEnemyEvent birth=wire(sent[n-1],2);unsigned before=n;
 assert(!sst_enemies_exchange(&a,&birth,capture,NULL));assert(n==before+1&&sent[n-1].snapshot&&sent[n-1].health==3);
 SstEnemyEvent answer=wire(sent[n-1],1);SstEnemyTrack *t=sst_enemies_exchange(&b,&answer,capture,NULL);assert(t&&t->health==3);
 before=n;assert(!sst_enemies_exchange(&a,&birth,capture,NULL));assert(n==before); // duplicate cannot provoke another reply
 // Receiving that write does not echo as local damage; equal snapshots do not ping-pong.
 sst_enemies_observe_live(&b,key,20,0x16d,3,capture,NULL);assert(n==before);
 SstEnemyEvent equal=answer;equal.from=2;equal.sequence=100;assert(!sst_enemies_exchange(&a,&equal,capture,NULL));assert(n==before);
 // An unseen dead actor answers a late full-health spawn without needing a live native actor.
 sst_enemies_observe_live(&a,key,10,0x16d,0,capture,NULL);assert(!sent[n-1].snapshot&&sent[n-1].damage==3);
 sst_enemies_begin(&a,&scene,1);birth.sequence=101;before=n;assert(!sst_enemies_exchange(&a,&birth,capture,NULL));assert(n==before+1&&sent[n-1].snapshot&&!sent[n-1].health);
 answer=wire(sent[n-1],1);assert(sst_enemies_exchange(&b,&answer,capture,NULL)->health==0);
 before=n;equal.health=0;equal.sequence=102;assert(!sst_enemies_exchange(&a,&equal,capture,NULL));assert(n==before);
 // Stale nonzero despawns are not authority; a new visit forgets old defeats.
 sst_enemies_begin(&b,&scene,2);birth.from=3;birth.sequence=1;birth.presence_generation=2;assert(!sst_enemies_exchange(&b,&birth,capture,NULL));assert(n==before+1); // b is also defeated
 sst_enemies_begin(&a,&scene,3);before=n;birth.sequence=2;assert(!sst_enemies_exchange(&a,&birth,capture,NULL));assert(n==before);
 sst_enemies_observe_live(&a,key,30,0x16d,4,capture,NULL);assert(n==before+1&&sent[n-1].health==4);
 sst_enemies_begin(&a,&scene,3);before=n;birth.sequence=3;birth.health=8;assert(!sst_enemies_exchange(&a,&birth,capture,NULL));assert(n==before);
 // New incarnation announces again; malformed keys, bosses, trials and invalid health never do.
 sst_enemies_observe_live(&a,key,31,0x16d,4,capture,NULL);assert(n==before+1);before=n;
 sst_enemies_observe_live(&a,key,32,0x16d,1001,capture,NULL);sst_enemies_observe_live(&a,key,0,0x16d,4,capture,NULL);
 sst_enemies_observe_live(&a,key^0x10000,32,0x16d,4,capture,NULL);sst_enemies_observe_live(&a,sst_world_enemy_key(0x193,2,0),32,0x193,4,capture,NULL);assert(n==before);
 strcpy(scene.stage,"B100");sst_enemies_begin(&a,&scene,4);sst_enemies_observe_live(&a,key,40,0x16d,4,capture,NULL);assert(n==before);
 strcpy(scene.stage,"S000");sst_enemies_begin(&a,&scene,5);sst_enemies_observe_live(&a,key,50,0x16d,4,capture,NULL);assert(n==before);
 // Four peers converge with bounded replies, including deletion and a same-visit respawn.
 static SstEnemies peers[4]={0};strcpy(scene.stage,"F006r");
 for(unsigned i=0;i<4;i++){sst_enemies_begin(&peers[i],&scene,10+i);sst_enemies_observe(&peers[i],key,100+i,0x16d,i==0?0:4,capture,NULL);}
 SstEnemyEvent first={.scene=scene,.key=key,.profile=0x16d,.health=4,.snapshot=true,.sequence=1,.from=4,.presence_generation=1};
 n=0;for(unsigned j=0;j<3;j++)sst_enemies_exchange(&peers[j],&first,capture,NULL);assert(n==1&&sent[0].health==0);
 answer=wire(sent[0],1);before=n;for(unsigned j=1;j<4;j++){t=sst_enemies_exchange(&peers[j],&answer,capture,NULL);assert(t&&t->health==0);}assert(n==before);
 // Concurrent local hits still add once, and the subsequent exchange never adds damage.
 static SstEnemies x={0},y={0};sst_enemies_begin(&x,&scene,70);sst_enemies_begin(&y,&scene,71);
 sst_enemies_observe(&x,key,70,0x16d,20,capture,NULL);sst_enemies_observe(&y,key,71,0x16d,20,capture,NULL);n=0;
 sst_enemies_observe_live(&x,key,70,0x16d,17,capture,NULL);sst_enemies_observe_live(&y,key,71,0x16d,16,capture,NULL);
 SstEnemyEvent hx=wire(sent[0],1),hy=wire(sent[1],2);
 assert(sst_enemies_exchange(&x,&hy,capture,NULL)->health==13);assert(sst_enemies_exchange(&y,&hx,capture,NULL)->health==13);assert(n==2);
 hx.sequence=10;hx.health=20;hx.damage=0;hx.snapshot=true;before=n;assert(!sst_enemies_exchange(&y,&hx,capture,NULL));assert(n==before+1&&sent[n-1].health==13&&!sent[n-1].damage);
 answer=wire(sent[n-1],2);assert(!sst_enemies_exchange(&x,&answer,capture,NULL));assert(n==before+1);
 // All 256 placed actors announce once, with no per-frame spam.
 static SstEnemies many={0};sst_enemies_begin(&many,&scene,99);n=0;
 for(unsigned i=0;i<256;i++){sst_enemies_observe_live(&many,sst_world_enemy_key(0x16d,i,0),i+1,0x16d,4,capture,NULL);}assert(n==256);
 for(unsigned i=0;i<256;i++){sst_enemies_observe_live(&many,sst_world_enemy_key(0x16d,i,0),i+1,0x16d,4,capture,NULL);}assert(n==256);
 puts("Enemy spawn exchange PASS: late actors, wounds, retained defeats, dedupe, no echo, visit/incarnation reset, four peers and 256 actors");
}

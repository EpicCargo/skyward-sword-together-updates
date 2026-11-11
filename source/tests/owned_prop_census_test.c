// Owned synthetic address space; no fixed mappings, sockets or native game.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "owned_prop_census.h"
static unsigned char bytes[0x10000];
static const uint64_t low=UINT64_C(0x7000000000);
static unsigned reads;
static bool mutate;
static bool mutate_held;
static bool reader(void *context,uint64_t at,void *out,size_t n) {
    (void)context;assert(at>=low && at<low+sizeof bytes && n<=low+sizeof bytes-at);
    if(mutate && at==low+0x4000 && n==24 && ++reads==2)bytes[0x4008]++;
    if(mutate_held && at==low+0x2b90 && n==8 && ++reads==2)bytes[0x2b90]=1;
    memcpy(out,bytes+(at-low),n);return true;
}
static void put(uint64_t at,uint64_t v,unsigned n) {for(unsigned i=0;i<n;i++)bytes[at-low+i]=(unsigned char)(v>>(8*i));}
static void setup(void) {
    memset(bytes,0,sizeof bytes);reads=0;mutate=mutate_held=false;
    uint64_t player=low+0x1000,pot=low+0x4000;
    put(player,0x7001637d28,8);put(player+8,7,4);put(player+0x10,200,2);put(player+0x12,1,1);
    put(player+0x18+0x20,player,8);put(player+0x18+8,pot+0x18,8);
    put(pot,0x700162f008,8);put(pot+8,42,4);put(pot+0xc,0xfffffff0,4);put(pot+0x10,496,2);put(pot+0x12,1,1);
    put(pot+0x18,player+0x18,8);put(pot+0x18+0x20,pot,8);put(pot+0x136,117,2);
    float pos[3]={100,1.5f,-200};memcpy(bytes+0x4144,pos,sizeof pos);
}
int main(void) {
    OwnedCensusReader r={.read=reader,.low=low,.high=low+sizeof bytes};OwnedPropSnapshot out;
    setup();assert(owned_prop_census(&r,low+0x1000,&out));assert(out.nodes==2 && out.count==1);
    assert(out.items[0].placed_id==117 && out.items[0].instance==42 && out.items[0].profile==496);
    put(low+0x2b90,low+0x4000,8);assert(owned_prop_census(&r,low+0x1000,&out));assert(out.items[0].held_by_native);
    setup();put(low+0x4018+0x18,low+0x4018,8);memset(&out,0xa5,sizeof out);OwnedPropSnapshot old=out;
    assert(!owned_prop_census(&r,low+0x1000,&out));assert(!memcmp(&out,&old,sizeof out));
    setup();put(low+0x1018,low+0x1018,8);assert(!owned_prop_census(&r,low+0x1000,&out));
    setup();put(low+0x4018+0x20,low+0x4008,8);assert(!owned_prop_census(&r,low+0x1000,&out));
    setup();put(low+0x1018+8,low+sizeof bytes-8,8);assert(!owned_prop_census(&r,low+0x1000,&out));
    setup();mutate=true;assert(!owned_prop_census(&r,low+0x1000,&out));
    setup();mutate_held=true;assert(!owned_prop_census(&r,low+0x1000,&out));
    // A second real tree node with the same placement key must remain visible:
    // this observer does not reuse the production bridge's unique-key binding.
    setup();memcpy(bytes+0x6000,bytes+0x4000,0x1400);put(low+0x6008,43,4);
    put(low+0x6018+0x20,low+0x6000,8);put(low+0x4018+0x18,low+0x6018,8);
    assert(owned_prop_census(&r,low+0x1000,&out) && out.count==2 && out.nodes==3);
    assert(out.items[0].placed_id==117 && out.items[1].placed_id==117);
    assert(out.items[0].actor!=out.items[1].actor && out.items[0].instance!=out.items[1].instance);
    setup();put(low+0x4000,0x70015f6290,8);put(low+0x4010,521,2);put(low+0x5367,2,1);
    assert(owned_prop_census(&r,low+0x1000,&out) && out.count==0);
    put(low+0x5367,0,1);assert(owned_prop_census(&r,low+0x1000,&out) && out.count==1);
    puts("PASS owned read-only census: typed records/held identity, cycles, bounds, self links, torn identities, normal-barrel gate; unchanged output on failure");
    return 0;
}

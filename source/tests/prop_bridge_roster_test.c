// Actual bridge tick/discovery/state, owned synthetic arena and network stubs.
// No fixed guest mapping, model execution, sockets, native game or GPU.
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../engine/runtime/hle.h"
#include "../client/src/sst_game.h"
#include "../client/src/sst_world.h"

static unsigned char *arena;
static uint64_t arena_lo, arena_hi;
#undef GUEST_LO
#undef GUEST_HI
#define GUEST_LO arena_lo
#define GUEST_HI arena_hi
static bool ptr(uint64_t at, size_t count) { return at >= arena_lo && at < arena_hi && count <= arena_hi - at; }
static uint64_t r64(uint64_t at) { uint64_t v; assert(ptr(at, 8)); memcpy(&v, (void *)(uintptr_t)at, 8); return v; }
static uint32_t r32(uint64_t at) { uint32_t v; assert(ptr(at, 4)); memcpy(&v, (void *)(uintptr_t)at, 4); return v; }
static uint16_t r16(uint64_t at) { uint16_t v; assert(ptr(at, 2)); memcpy(&v, (void *)(uintptr_t)at, 2); return v; }
static uint8_t r8(uint64_t at) { assert(ptr(at, 1)); return *(uint8_t *)(uintptr_t)at; }
static void w32(uint64_t at, uint32_t v) { assert(ptr(at, 4)); memcpy((void *)(uintptr_t)at, &v, 4); }
static void put64(uint64_t at, uint64_t v) { assert(ptr(at, 8)); memcpy((void *)(uintptr_t)at, &v, 8); }
static void put16(uint64_t at, uint16_t v) { assert(ptr(at, 2)); memcpy((void *)(uintptr_t)at, &v, 2); }

#include "../engine/runtime/hle/sst_prop_bridge.h"

SstSettings g_sst = {.sync_props = true};
// The fixture calls prop_tick/discovery only. ASan can retain otherwise-unused
// callback references; fail loudly if any execution/model path is ever entered.
const SstGameLayout SST_LAYOUT_SSHD_100 = {0};
void guest_call(Cpu *cpu, uint64_t address) { (void)cpu; (void)address; assert(!"Unexpected native model call"); }
uint32_t sst_game_pose_tick(void) { assert(!"Unexpected native pose read"); return 0; }
SstLocal sst_game_read_local(const SstGameLayout *layout, uint32_t tick) {
    (void)layout; (void)tick; assert(!"Unexpected native local-reader call"); return (SstLocal){0};
}
void sst_net_send_prop(const SstProp *p) { (void)p; assert(!"Unexpected native prop send"); }
bool sst_remote_carry_displayed(const SstScene *scene, uint16_t id, uint32_t presence, SstPose *out) {
    (void)scene; (void)id; (void)presence; (void)out;
    assert(!"Unexpected native presentation lookup"); return false;
}
static uint64_t now_ms = 1000;
static uint32_t network_generation = 1;
static uint16_t local_id = 1;
static bool online = true;
static SstPlayer current[SST_MAX_PLAYERS];
static int current_count;
static SstProp queued[8];
static unsigned queued_count, discarded;
uint64_t sst_now_ms(void) { return now_ms; }
uint32_t sst_net_generation(void) { return network_generation; }
uint16_t sst_net_my_id(void) { return local_id; }
SstStatus sst_net_status(char *why, int bytes) { if (why && bytes > 0) *why = 0; return online ? SST_ONLINE : SST_OFFLINE; }
int sst_net_players(SstPlayer *out, int capacity) {
    assert(current_count <= capacity); memcpy(out, current, (size_t)current_count * sizeof *out); return current_count;
}
void sst_net_poll_props(SstPropFn fn, void *user) {
    unsigned n = queued_count; queued_count = 0;
    for (unsigned i = 0; i < n; i++) { if (fn) fn(&queued[i], user); else discarded++; }
}
static const SstScene area = {.stage="F000", .room=1, .layer=1, .entrance=4};
static SstProp prop(unsigned key, uint16_t from, uint32_t generation, uint32_t seq) {
    return (SstProp){.scene=area, .key={.placed_id=(uint16_t)key, .profile=SST_PROP_PROFILE,
        .params=0xfffffff0, .room=1, .layer=1}, .from=from, .presence_generation=generation,
        .sequence=seq, .pose_tick=100, .phase=SST_PROP_PLACE, .received_ms=now_ms,
        .matrix={1,0,0,0,0,1,0,0,0,0,1,0}};
}
static void enqueue(SstProp p) { assert(queued_count < 8); queued[queued_count++] = p; }
static void tick(SstLocal local, uint32_t load) { prop_tick(local, load, true, false); }
static bool get(unsigned key, SstProp *out) {
    SstProp p=prop(key,0,0,0); return sst_props_get(&props_bridge.state, &p.key, now_ms, out);
}
int main(void) {
    arena=calloc(1,0x10000); assert(arena);
    arena_lo=(uint64_t)(uintptr_t)arena; arena_hi=arena_lo+0x10000;
    uint64_t player=arena_lo+0x1000, vase=arena_lo+0x4000;
    put16(player+0x10,0xc8); arena[player-arena_lo+0x12]=1;
    put64(player+0x18+0x20,player); put64(player+0x18+8,vase+0x18);
    // All four keys whose ownership/watermarks are checked below must have
    // actual eligible native actors. Absent keys belong to admission tests.
    for(unsigned i=0;i<4;i++) {
        uint64_t a=vase+(uint64_t)i*0x2000;
        put64(a+0x18,player+0x18);put64(a+0x38,a);
        put64(a+0x30,i<3?a+0x2000+0x18:0);
        put64(a,SST_PROP_TUBO_VTABLE);w32(a+8,42+i);w32(a+0xc,0xfffffff0);
        put16(a+0x10,SST_PROP_PROFILE);arena[a-arena_lo+0x12]=1;
        put16(a+0x136,(uint16_t)(9+i));arena[a-arena_lo+0x17c]=area.room;
    }
    SstLocal local={.valid=true, .scene=area, .player_address=player, .player_instance=1};
    for (unsigned i=0;i<3;i++) current[i]=(SstPlayer){.active=true, .id=(uint16_t)(i+2),
        .presence_generation=i+7, .scene=area};
    current_count=3;
    for (unsigned i=0;i<3;i++) enqueue(prop(9+i,current[i].id,current[i].presence_generation,1));
    tick(local,1);
    assert(props_bridge.enabled && sst_prop_discovery_stats.last_nodes==5 && sst_prop_discovery_stats.last_bindings==4);
    SstProp out;
    assert(get(9,&out) && out.from==2);
    SstProp own=prop(12,1,1,1);
    assert(sst_props_update(&props_bridge.state,1,1,&own,now_ms));
    for (unsigned i=0;i<1000;i++) {
        SstPlayer old=current[0]; now_ms++;
        current[0].id=(uint16_t)(5+i); current[0].presence_generation=20+i;
        enqueue(prop(9,old.id,old.presence_generation,999));
        enqueue(prop(9,current[0].id,current[0].presence_generation,1));
        tick(local,1);
        assert(get(9,&out) && out.from==current[0].id && out.sequence==1);
        assert(get(10,&out) && out.from==3 && out.sequence==1);
        assert(get(12,&out) && out.from==1 && out.presence_generation==1);
    }
    puts("actual bridge: 1000 replacements; stable remote/local streams retained; retired queued tuples rejected");
    current[0].scene.room=2; tick(local,1); assert(!get(9,&out));
    enqueue(prop(9,current[0].id,current[0].presence_generation,999)); tick(local,1); assert(!get(9,&out));
    current[0].scene=area;
    enqueue(prop(9,current[0].id,current[0].presence_generation,2)); tick(local,1);
    assert(get(9,&out) && out.sequence==2);
    puts("actual bridge: same-socket area return requires and receives fresh roster authority");
    SstPlayer saved=current[1]; current[1]=current[0]; tick(local,1);
    assert(!props_bridge.enabled && !props_bridge.state.initialized);
    current[1]=saved; tick(local,1); assert(props_bridge.enabled && !get(9,&out));
    local_id=0; tick(local,1); assert(!props_bridge.enabled);
    local_id=1; tick(local,1); assert(props_bridge.enabled);
    enqueue(prop(9,current[0].id,current[0].presence_generation,3));
    prop_tick(local,1,true,true); assert(!props_bridge.enabled && !queued_count && discarded==1);
    tick(local,1); assert(props_bridge.enabled && !get(9,&out));
    // This binding was recreated after invalid-roster suspension. Its receive
    // fence deliberately rejects a packet tied to the discovery millisecond.
    now_ms++;
    enqueue(prop(9,current[0].id,current[0].presence_generation,4)); tick(local,1); assert(get(9,&out));
    tick(local,2); assert(!get(9,&out));
    enqueue(prop(9,current[0].id,current[0].presence_generation,5)); tick(local,2); assert(get(9,&out));
    network_generation++; tick(local,2); assert(!get(9,&out));
    enqueue(prop(9,current[0].id,current[0].presence_generation,6));
    prop_tick(local,2,false,false); assert(!props_bridge.enabled && !queued_count && discarded==2);
    puts("actual bridge: invalid roster, missing local identity, pause, load/reconnect and disabled gates clear or drain safely");
    sst_prop_bridge_reset(); free(arena);
    puts("prop bridge roster CPU checks PASS; synthetic arena and network stubs; no native model/game/GPU acceptance");
    return 0;
}

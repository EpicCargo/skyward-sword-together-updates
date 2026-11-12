// Actual prop_execute/native adapter code over an owned arena and a synthetic
// model vtable. Model calls and network/game reads are stubs: no game or GPU.
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine/runtime/hle.h"
#include "../client/src/sst_game.h"
#include "../client/src/sst_world.h"

#ifndef SST_PROP_FIXTURE_ARENA_SIZE
#define SST_PROP_FIXTURE_ARENA_SIZE 0x10000
#endif
static unsigned char *arena;
static uint64_t arena_lo, arena_hi;
static uint64_t actor, player, model;
static bool ptr(uint64_t, size_t);
static uint64_t r64(uint64_t);
static uint32_t r32(uint64_t);
static uint16_t r16(uint64_t);
static uint8_t r8(uint64_t);
static void w32(uint64_t, uint32_t);
#undef GUEST_LO
#undef GUEST_HI
#define GUEST_LO arena_lo
#define GUEST_HI arena_hi
#include "../engine/runtime/hle/sst_prop_bridge.h"

static bool arena_range(uint64_t at, size_t count) {
    return at >= arena_lo && at < arena_hi && count <= arena_hi - at;
}
static bool ptr(uint64_t at, size_t count) {
    return arena_range(at, count) || (at >= SST_PROP_MODEL_VTABLE &&
        at < SST_PROP_MODEL_VTABLE + 0x40 && count <= SST_PROP_MODEL_VTABLE + 0x40 - at);
}
static uint64_t r64(uint64_t at) {
    // Only these two synthetic virtual targets are readable outside the arena.
    if (at == SST_PROP_MODEL_VTABLE + 8) return SST_PROP_MODEL_PROCESS;
    if (at == SST_PROP_MODEL_VTABLE + 0x38) return SST_PROP_MODEL_OPTIONS;
    uint64_t v; assert(arena_range(at, sizeof v)); memcpy(&v, (void *)(uintptr_t)at, sizeof v); return v;
}
static uint32_t r32(uint64_t at) {
    uint32_t v; assert(arena_range(at, sizeof v)); memcpy(&v, (void *)(uintptr_t)at, sizeof v); return v;
}
static uint16_t r16(uint64_t at) {
    uint16_t v; assert(arena_range(at, sizeof v)); memcpy(&v, (void *)(uintptr_t)at, sizeof v); return v;
}
static uint8_t r8(uint64_t at) { assert(arena_range(at, 1)); return *(uint8_t *)(uintptr_t)at; }
static void w32(uint64_t at, uint32_t v) { assert(arena_range(at, 4)); memcpy((void *)(uintptr_t)at, &v, 4); }
static void put64(uint64_t at, uint64_t v) { assert(arena_range(at, 8)); memcpy((void *)(uintptr_t)at, &v, 8); }
static void put16(uint64_t at, uint16_t v) { assert(arena_range(at, 2)); memcpy((void *)(uintptr_t)at, &v, 2); }
static void put8(uint64_t at, uint8_t v) { assert(arena_range(at, 1)); *(uint8_t *)(uintptr_t)at = v; }

SstSettings g_sst = {.sync_props = true};
const SstGameLayout SST_LAYOUT_SSHD_100 = {0};
static uint64_t now_ms;
static uint16_t local_player_id = 1;
static bool network_online = true, remote_connected = true;
static uint32_t network_generation = 1, remote_presence = 7, displayed_presence = 7;
static SstScene remote_scene;
static SstLocal local;
static SstPose displayed;
static bool body_visible;
static unsigned model_calls, original_calls, sends;
static SstProp captured;
static SstProp pending;
static bool have_pending;
static Cpu cpu;
static const SstScene area = {.stage="F000", .room=1, .layer=1, .entrance=4};

void guest_call(Cpu *c, uint64_t address) {
    assert(pthread_mutex_trylock(&props_bridge.mu) == 0);
    pthread_mutex_unlock(&props_bridge.mu);
    assert(c->x[0] == model);
    if (address == SST_PROP_MODEL_OPTIONS) assert(c->x[1] == 2 && c->x[2] <= 1);
    else { assert(address == SST_PROP_MODEL_PROCESS); assert(c->x[1] == 1 && !c->x[2] && !c->x[3]); }
    model_calls++;
    c->x[1] = 0; // The actual adapter must restore the caller's register state.
}
uint32_t sst_game_pose_tick(void) { return local.pose.tick; }
SstLocal sst_game_read_local(const SstGameLayout *layout, uint32_t tick) {
    assert(layout == &SST_LAYOUT_SSHD_100 && tick == local.pose.tick); return local;
}
void sst_net_send_prop(const SstProp *p) { assert(sst_prop_valid(p)); captured = *p; sends++; }
bool sst_remote_carry_displayed(const SstScene *scene, uint16_t id, uint32_t presence, SstPose *out) {
    if (!body_visible || id != 2 || presence != displayed_presence || !sst_world_same_area(scene, &area)) return false;
    *out = displayed; return true;
}
uint64_t sst_now_ms(void) { return now_ms; }
uint32_t sst_net_generation(void) { return network_generation; }
uint16_t sst_net_my_id(void) { return local_player_id; }
SstStatus sst_net_status(char *why, int bytes) { if (why && bytes > 0) *why = 0; return network_online ? SST_ONLINE : SST_OFFLINE; }
int sst_net_players(SstPlayer *out, int capacity) {
    if (!remote_connected) return 0;
    assert(capacity >= 1); out[0] = (SstPlayer){.active=true, .id=2, .presence_generation=remote_presence, .scene=remote_scene}; return 1;
}
void sst_net_poll_props(SstPropFn fn, void *user) {
    if (have_pending) { have_pending = false; if (fn) fn(&pending, user); }
}
static void original(Cpu *c) { assert(c->x[0] == actor); original_calls++; c->x[0] = 1; }
static void execute(void) {
    cpu.x[0] = actor; cpu.x[1] = 0x1234;
    prop_execute(&cpu, original);
    assert(cpu.x[0] == 1 && cpu.x[1] == 0x1234);
}
static void tick(void) { prop_tick(local, 1, true, false); }
static void actual_pose(float x, float y, float z) {
    SstPropNativePose p = {.pos={x,y,z}, .matrix={1,0,0,x,0,1,0,y,0,0,1,z}};
    memcpy((void *)(uintptr_t)(actor + 0x144), p.pos, sizeof p.pos);
    memcpy((void *)(uintptr_t)(actor + 0x13c), p.rot, sizeof p.rot);
    memcpy((void *)(uintptr_t)(actor + 0x1f4), p.matrix, sizeof p.matrix);
}
static void expect_position(float x, float y, float z) {
    float p[3]; memcpy(p, (void *)(uintptr_t)(actor + 0x144), sizeof p);
    assert(fabsf(p[0]-x) < .001f && fabsf(p[1]-y) < .001f && fabsf(p[2]-z) < .001f);
}
static void expect_unheld(void) {
    assert(r64(player + 0x1b90) == 0 && r64(actor + 0x2d0) == 0 && r64(actor + 0x2d8) == 0);
    assert(r32(actor + 0x358) == 0 && r32(actor + 0x2e4) == 0);
}
static void send_remote(uint32_t sequence, SstPropPhase phase) {
    pending = (SstProp){.scene=area, .key=prop_binding(actor)->key, .from=2,
        .presence_generation=remote_presence, .sequence=sequence, .pose_tick=100, .phase=phase,
        .received_ms=now_ms, .origin={100,1.5f,-100}};
    float x = phase == SST_PROP_CARRY ? 100 : 500;
    float y = phase == SST_PROP_CARRY ? 101.5f : 1.5f;
    float z = phase == SST_PROP_CARRY ? -100 : 600;
    const float matrix[12] = {1,0,0,x,0,1,0,y,0,0,1,z};
    pending.pos[0]=x;pending.pos[1]=y;pending.pos[2]=z;
    memcpy(pending.matrix, matrix, sizeof matrix);
    assert(sst_prop_valid(&pending)); have_pending = true; tick();
}
static void setup(const SstPropNativeDescriptor *d) {
    network_online = remote_connected = true;
    network_generation = 1; remote_presence = displayed_presence = 7; remote_scene = area;
    sst_prop_bridge_reset(); memset(&sst_prop_presentation_stats, 0, sizeof sst_prop_presentation_stats);
    arena=calloc(1,SST_PROP_FIXTURE_ARENA_SIZE);assert(arena);arena_lo=(uint64_t)(uintptr_t)arena;arena_hi=arena_lo+SST_PROP_FIXTURE_ARENA_SIZE;
    player=arena_lo+0x1000;actor=arena_lo+0x4000;model=arena_lo+0x8000;
    put16(player+0x10,0xc8);put8(player+0x12,1);
    put64(player+0x18+0x20,player);put64(player+0x18+8,actor+0x18);
    put64(actor+0x18,player+0x18);put64(actor+0x18+0x20,actor);
    put64(actor,d->vtable);w32(actor+8,42);w32(actor+0xc,0xfffffff0);
    put16(actor+0x10,d->profile);put8(actor+0x12,1);put16(actor+0x136,9);put8(actor+0x17c,area.room);
    put64(actor+d->model_offset,model);put64(model,SST_PROP_MODEL_VTABLE);
    float scale[3]={1,1,1};memcpy((void *)(uintptr_t)(actor+0x150),scale,sizeof scale);
    actual_pose(10,1.5f,20);
    now_ms=1000;local=(SstLocal){.valid=true,.scene=area,.player_address=player,.player_instance=1};
    local.pose.tick=100;local.pose.pos[0]=10;local.pose.pos[1]=1.5f;local.pose.pos[2]=20;
    displayed=(SstPose){.pos={1000,1.5f,-1000}};body_visible=true;
    model_calls=original_calls=sends=0;have_pending=false;cpu=(Cpu){0};tick();
    assert(props_bridge.enabled && prop_binding(actor));
}
static void finish(void) { sst_prop_bridge_reset();free(arena);arena=NULL;arena_lo=arena_hi=0; }

static void presentation_lifecycle(const SstPropNativeDescriptor *d) {
    setup(d);execute();now_ms=1101;execute();
    assert(prop_binding(actor)->rest.ready && original_calls==2 && !sends);
    now_ms=1102;send_remote(1,SST_PROP_CARRY);execute();
    expect_position(1000,101.5f,-1000);expect_unheld();
    assert(original_calls==2 && prop_binding(actor)->remote_held);
    assert(model_calls == ((d->tail_flags&SST_PROP_NATIVE_CALC_TASKS)?3u:0u));
    now_ms=1200;displayed.pos[0]=1050;send_remote(2,SST_PROP_CARRY);execute();
    expect_position(1050,101.5f,-1000);expect_unheld();
    assert(prop_binding(actor)->rest.pose.pos[0]==10);
    now_ms=2801;tick();execute();
    expect_position(10,1.5f,20);expect_unheld();
    assert(!prop_binding(actor)->remote_held && sst_prop_presentation_stats.parked_presented==1);
    now_ms=2802;body_visible=false;send_remote(3,SST_PROP_CARRY);execute();
    expect_position(10,1.5f,20);expect_unheld();assert(!prop_binding(actor)->remote_held);
    now_ms=2803;body_visible=true;send_remote(4,SST_PROP_CARRY);execute();
    expect_position(1050,101.5f,-1000);assert(prop_binding(actor)->remote_held);
    now_ms=2900;send_remote(5,SST_PROP_PLACE);execute();
    expect_position(500,1.5f,600);expect_unheld();
    assert(!prop_binding(actor)->remote_held && !prop_binding(actor)->rest.ready);
    now_ms=3001;execute();now_ms=3102;execute();
    expect_position(500,1.5f,600);assert(prop_binding(actor)->rest.ready);
    now_ms=3103;send_remote(6,SST_PROP_CARRY);execute();expect_position(1050,101.5f,-1000);
    // A new lifetime at the same address cannot use the old binding or floor sample.
    w32(actor+8,43);actual_pose(35,1.5f,45);unsigned before=original_calls;
    execute();expect_position(35,1.5f,45);assert(original_calls==before+1);
    now_ms=3200;tick();execute();expect_position(35,1.5f,45);
    assert(prop_binding(actor)->instance==43 && !prop_binding(actor)->rest.ready);
    assert(!prop_binding(actor)->remote_held && !sends);
    printf("profile%u: moving carry, stale/body-missing restoration, PLACE, and actor lifetime gates PASS\n",d->profile);
    finish();
}
static void locally_held_actor(const SstPropNativeDescriptor *d) {
    setup(d);send_remote(1,SST_PROP_CARRY);
    const uint64_t backlink=arena_lo+0x7000;
    put64(player+0x1b90,actor);w32(actor+0x358,1);w32(actor+0x2e4,1);
    put64(actor+0x2d0,player);put64(actor+0x2d8,backlink);put64(backlink+0x10,actor);
    actual_pose(10,101.5f,20);execute();expect_position(10,101.5f,20);
    assert(original_calls==1 && !model_calls && sends==1 && captured.phase==SST_PROP_CARRY);
    assert(r64(player+0x1b90)==actor && r64(actor+0x2d0)==player && r64(actor+0x2d8)==backlink);
    assert(r32(actor+0x358)==1 && r32(actor+0x2e4)==1 && !prop_binding(actor)->remote_held);
    printf("profile%u: local held object retains native path and linkage PASS\n",d->profile);
    finish();
}
int main(void) {
    for(unsigned i=0;i<SST_PROP_NATIVE_ADAPTERS;i++) {
        presentation_lifecycle(&sst_prop_native_descriptors[i]);
        locally_held_actor(&sst_prop_native_descriptors[i]);
    }
    puts("Actual bridge presentation CPU checks PASS; synthetic arena/model calls only, no game/GPU acceptance.");
    return 0;
}

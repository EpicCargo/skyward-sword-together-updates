// Real bridge discovery over an owned arena. No game, model, sockets or GPU.
#define main roster_fixture_main
#include "prop_bridge_roster_test.c"
#undef main

static uint64_t root_player, actors[SST_PROPS+1];
static SstLocal sample_local;
static void make_actor(unsigned i,unsigned key) {
    uint64_t a=actors[i];
    memset((void *)(uintptr_t)a,0,0x2000);
    put64(a,SST_PROP_TUBO_VTABLE);w32(a+8,100+i);w32(a+0xc,0xfffffff0);
    put16(a+0x10,SST_PROP_PROFILE);arena[a-arena_lo+0x12]=1;
    put16(a+0x136,key);arena[a-arena_lo+0x17c]=area.room;
    put64(a+0x18,root_player+0x18);put64(a+0x38,a);
}
static void setup_tree(unsigned count) {
    sst_prop_bridge_reset();arena=calloc(1,0x100000);assert(arena);
    arena_lo=(uint64_t)(uintptr_t)arena;arena_hi=arena_lo+0x100000;
    root_player=arena_lo+0x1000;
    put16(root_player+0x10,200);arena[root_player-arena_lo+0x12]=1;
    put64(root_player+0x38,root_player);
    for(unsigned i=0;i<SST_PROPS+1;i++){actors[i]=arena_lo+0x4000+i*0x2000;make_actor(i,i+10);}
    put64(root_player+0x20,actors[0]+0x18);
    for(unsigned i=0;i<count;i++)put64(actors[i]+0x30,i+1<count?actors[i+1]+0x18:0);
    sample_local=(SstLocal){.valid=true,.scene=area,.player_address=root_player,.player_instance=1};
    current_count=queued_count=0;local_id=network_generation=1;online=true;
}
static void finish_tree(void) {sst_prop_bridge_reset();free(arena);arena=NULL;}
static void overflow_duplicate(void) {
    setup_tree(SST_PROPS+1);put16(actors[SST_PROPS]+0x136,10);tick(sample_local,1);
    assert(!props_bridge.enabled && !prop_binding(actors[0]));
    puts("PASS duplicate beyond binding capacity disables ambiguous discovery");finish_tree();
}
static void replace_instance(void) {
    setup_tree(SST_PROPS);tick(sample_local,1);assert(props_bridge.enabled);
    prop_binding(actors[0])->rest.ready=true;
    w32(actors[SST_PROPS-1]+8,999);tick(sample_local,1);
    assert(props_bridge.enabled && sst_prop_discovery_stats.last_bindings==SST_PROPS);
    assert(prop_binding(actors[SST_PROPS-1]) && prop_binding(actors[SST_PROPS-1])->instance==999);
    assert(prop_binding(actors[0])->rest.ready);
    puts("PASS full table rebinds a changed instance in one discovery and retains stable evidence");finish_tree();
}
static void replace_address(void) {
    setup_tree(SST_PROPS);tick(sample_local,1);prop_binding(actors[1])->rest.ready=true;
    put64(root_player+0x20,actors[1]+0x18);
    put64(actors[SST_PROPS-1]+0x30,actors[SST_PROPS]+0x18);
    tick(sample_local,1);
    assert(props_bridge.enabled && sst_prop_discovery_stats.last_bindings==SST_PROPS);
    assert(!prop_binding(actors[0]) && prop_binding(actors[SST_PROPS]));
    assert(prop_binding(actors[1])->rest.ready);
    puts("PASS removed actor frees capacity before a new actor is bound");finish_tree();
}
static void identity_change(void) {
    setup_tree(1);tick(sample_local,1);prop_binding(actors[0])->rest.ready=true;
    put16(actors[0]+0x136,77);tick(sample_local,1);
    PropBinding *b=prop_binding(actors[0]);assert(b && b->key.placed_id==77 && !b->rest.ready);
    b->rest.ready=true;put16(actors[0]+0x10,SST_PROP_BIG_PROFILE);put64(actors[0],SST_PROP_BIG_VTABLE);
    tick(sample_local,1);b=prop_binding(actors[0]);assert(b && b->key.profile==SST_PROP_BIG_PROFILE && !b->rest.ready);
    puts("PASS placement/profile changes cannot inherit the old binding or rest pose");finish_tree();
}
static void callback_identity(void) {
    setup_tree(1);tick(sample_local,1);assert(prop_binding(actors[0]));
    put16(actors[0]+0x10,SST_PROP_BIG_PROFILE);put64(actors[0],SST_PROP_BIG_VTABLE);
    assert(!prop_binding(actors[0]));
    put16(actors[0]+0x10,SST_PROP_PROFILE);put64(actors[0],SST_PROP_TUBO_VTABLE);
    arena[actors[0]-arena_lo+0x17c]=2;assert(!prop_binding(actors[0]));
    arena[actors[0]-arena_lo+0x17c]=0xff;assert(prop_binding(actors[0]));
    puts("PASS execute lookup rechecks profile/room and admits the reviewed global-room marker");finish_tree();
}
static void rejected_trees(void) {
    setup_tree(SST_PROPS+1);tick(sample_local,1);assert(!props_bridge.enabled);
    put64(actors[SST_PROPS-1]+0x30,0);tick(sample_local,1);assert(props_bridge.enabled);
    put16(actors[1]+0x136,10);tick(sample_local,1);assert(!props_bridge.enabled);
    put16(actors[1]+0x136,11);w32(actors[2]+8,0);tick(sample_local,1);assert(!props_bridge.enabled);
    w32(actors[2]+8,102);tick(sample_local,1);assert(props_bridge.enabled);
    put64(actors[SST_PROPS-1]+0x30,actors[0]+0x18);tick(sample_local,1);assert(!props_bridge.enabled);
    for(unsigned i=0;i<SST_PROPS;i++)assert(!props_bridge.bindings[i].actor);
    puts("PASS unique overflow, duplicate, zero-instance and cyclic trees clear all authority; valid tree recovers");finish_tree();
}
static void full_table_churn(void) {
    setup_tree(SST_PROPS+1);
    // An out-of-room eligible class does not consume this room's capacity.
    arena[actors[SST_PROPS]-arena_lo+0x17c]=2;tick(sample_local,1);assert(props_bridge.enabled);
    prop_binding(actors[0])->rest.ready=true;
    for(unsigned i=0;i<1000;i++) {
        w32(actors[SST_PROPS-1]+8,1000+i);tick(sample_local,1);
        assert(props_bridge.enabled && sst_prop_discovery_stats.last_bindings==SST_PROPS);
        assert(prop_binding(actors[SST_PROPS-1])->instance==1000+i);
        assert(prop_binding(actors[0])->rest.ready && !prop_binding(actors[SST_PROPS-1])->rest.ready);
    }
    puts("PASS 1000 full-table actor replacements retain stable rest data and exclude other-room actors");finish_tree();
}
int main(int argc,char **argv) {
    assert(argc==2);int mode=atoi(argv[1]);
    if(mode==1)overflow_duplicate();else if(mode==2)replace_instance();
    else if(mode==3)replace_address();else if(mode==4)identity_change();
    else if(mode==5)callback_identity();else if(mode==6)rejected_trees();
    else if(mode==7)full_table_churn();else return 2;
    return 0;
}

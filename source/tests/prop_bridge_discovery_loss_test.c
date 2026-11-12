// Real bridge callbacks after discovery loses authority; synthetic native arena.
#define SST_PROP_FIXTURE_ARENA_SIZE 0x400000
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main

static void broken_tree(unsigned mode, const SstPropNativeDescriptor *d) {
    if(mode==3) {put64(actor+0x30,actor+0x18);return;}
    if(mode==4) {put64(actor+0x38,player);return;}
    unsigned count=mode==5?SST_PROP_WALK_NODES:SST_PROPS;
    for(unsigned i=0;i<count;i++) {
        uint64_t a=arena_lo+0x10000+(uint64_t)i*0x200;
        if(mode!=5) {
            // Full native allocations must not overlap. No model is executed
            // for these extra actors, so only discovery metadata is required.
            a=arena_lo+0x10000+(uint64_t)i*0x2000;
            put64(a,d->vtable);w32(a+8,100+i);w32(a+0xc,0xfffffff0);
            put16(a+0x10,d->profile);put8(a+0x12,1);
            put16(a+0x136,(uint16_t)(mode==2?9:10+i));put8(a+0x17c,area.room);
        }
        put64(a+0x18,player+0x18);put64(a+0x38,a);
        uint64_t next=i+1<count?a+(mode==5?0x200:0x2000)+0x18:0;
        put64(a+0x30,next);
    }
    put64(actor+0x30,arena_lo+0x10000+0x18);
}
static void invalid_tick(void) {
    unsigned char *before=malloc(SST_PROP_FIXTURE_ARENA_SIZE);assert(before);
    memcpy(before,arena,SST_PROP_FIXTURE_ARENA_SIZE);unsigned models=model_calls;
    tick();assert(!memcmp(before,arena,SST_PROP_FIXTURE_ARENA_SIZE) && models==model_calls);free(before);
    assert(!props_bridge.enabled && !props_bridge.state.initialized);
}
static void native_pickup(void) {
    uint64_t backlink=arena_lo+0x7000;
    put64(player+0x1b90,actor);w32(actor+0x358,1);w32(actor+0x2e4,1);
    put64(actor+0x2d0,player);put64(actor+0x2d8,backlink);put64(backlink+0x10,actor);
    actual_pose(10,101.5f,20);
}
static void check(const SstPropNativeDescriptor *d, unsigned mode) {
    setup(d);execute();
    if(mode!=12) {now_ms=1101;execute();assert(prop_binding(actor)->rest.ready);}
    now_ms=mode==12?1001:1102;
    if(mode!=14) {send_remote(1,SST_PROP_CARRY);execute();}
    if(mode!=12 && mode!=14) {
        expect_position(1000,101.5f,-1000);
        assert(prop_binding(actor)->remote_held && prop_binding(actor)->rest.ready);
    }
    if(mode==6)native_pickup();
    if(mode==7) {w32(actor+8,43);actual_pose(35,1.5f,45);}
    if(mode==8) {local.player_instance=2;actual_pose(35,1.5f,45);}
    if(mode==9) {w32(actor+0x358,2);actual_pose(35,1.5f,45);}
    if(mode==10)put64(model,0);
    if(mode==11) {send_remote(2,SST_PROP_PLACE);execute();expect_position(500,1.5f,600);}
    unsigned before=original_calls;now_ms=1200;
    broken_tree(mode,d);invalid_tick();invalid_tick();
    if(mode==13) {put64(actor+0x30,0);tick();assert(props_bridge.enabled);}
    execute();
    if(mode<=5 || mode==13) {
        expect_position(10,1.5f,20);expect_unheld();assert(original_calls==before);
        assert(!prop_binding(actor)->remote_held && !prop_binding(actor)->restore_pending);
        // No repeated saved-floor write after native simulation resumes.
        actual_pose(35,1.5f,45);execute();expect_position(35,1.5f,45);assert(original_calls==before+1);
        if(mode==13) {
            now_ms=1400;execute();now_ms=1501;execute();
            assert(prop_binding(actor)->rest.ready);
            now_ms=1502;send_remote(2,SST_PROP_CARRY);execute();expect_position(1000,101.5f,-1000);
        }
    } else {
        assert(original_calls==before+1);
        if(mode==6) {expect_position(10,101.5f,20);assert(sst_prop_native_held(actor,player));}
        else if(mode==7 || mode==8 || mode==9)expect_position(35,1.5f,45);
        else if(mode==10) {
            expect_position(1000,101.5f,-1000);put64(model,SST_PROP_MODEL_VTABLE);
            actual_pose(35,1.5f,45);execute();expect_position(35,1.5f,45);assert(original_calls==before+2);
        } else if(mode==11)expect_position(500,1.5f,600);
        else {expect_position(10,1.5f,20);assert(!prop_binding(actor));}
    }
    assert(!sends);
    printf("profile%u local%u discovery-loss mode%u PASS\n",d->profile,local_player_id,mode);
    finish();
}
int main(int argc, char **argv) {
    assert(argc==2 || argc==4);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode>=1 && mode<=14);
    if(argc==4) {
        const SstPropNativeDescriptor *d=sst_prop_native_descriptor((uint16_t)strtoul(argv[2],NULL,10));
        local_player_id=(uint16_t)strtoul(argv[3],NULL,10);assert(d && (local_player_id==1 || local_player_id==3));
        check(d,mode);
    } else for(unsigned i=0;i<SST_PROP_NATIVE_ADAPTERS;i++) {
        local_player_id=1;check(&sst_prop_native_descriptors[i],mode);
        local_player_id=3;check(&sst_prop_native_descriptors[i],mode);
    }
    return 0;
}

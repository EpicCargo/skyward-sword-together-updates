// Actual actor bridge with synthetic transport roster/native arena/model calls.
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main

static void depart_tick(void) {
    unsigned char before[0x10000];memcpy(before,arena,sizeof before);
    unsigned models=model_calls;tick();
    assert(!memcmp(before,arena,sizeof before) && model_calls==models);
    assert(props_bridge.enabled && network_online);
}
static void pickup(void) {
    uint64_t backlink=arena_lo+0x7000;
    put64(player+0x1b90,actor);w32(actor+0x358,1);w32(actor+0x2e4,1);
    put64(actor+0x2d0,player);put64(actor+0x2d8,backlink);put64(backlink+0x10,actor);
    actual_pose(10,101.5f,20);
}
static void check(const SstPropNativeDescriptor *d, unsigned mode) {
    setup(d);execute();now_ms=1101;execute();now_ms=1102;
    send_remote(1,SST_PROP_CARRY);execute();expect_position(1000,101.5f,-1000);
    assert(prop_binding(actor)->remote_held && prop_binding(actor)->rest.ready);
    SstProp retired;assert(sst_props_get(&props_bridge.state,&prop_binding(actor)->key,now_ms,&retired));
    unsigned before=original_calls;now_ms=1200;
    if(mode==9) {send_remote(2,SST_PROP_PLACE);execute();expect_position(500,1.5f,600);}
    if(mode==5)pickup();
    if(mode==6) {w32(actor+8,43);actual_pose(35,1.5f,45);}
    if(mode==7) {local.player_instance=2;actual_pose(35,1.5f,45);}
    if(mode==8) {w32(actor+0x358,2);actual_pose(35,1.5f,45);}
    if(mode==2 || mode==4)remote_presence=8;
    else if(mode==3) {strcpy(remote_scene.stage,"F002");remote_scene.room=2;}
    else remote_connected=false;
    if(mode==7)prop_tick(local,2,true,false);
    else depart_tick();
    // Even a high-sequence old packet cannot reauthorize the retired source.
    retired.sequence=999;retired.received_ms=now_ms;
    uint64_t received=sst_prop_presentation_stats.received;
    prop_incoming(&retired,NULL);assert(sst_prop_presentation_stats.received==received);
    if(mode==4) {
        // Same sequence as the old carry; the new incarnation owns this PLACE.
        send_remote(1,SST_PROP_PLACE);execute();expect_position(500,1.5f,600);
        assert(!prop_binding(actor)->remote_held && prop_binding(actor)->applied_generation==8);
    } else {
        execute();
        if(mode<=3) {
            expect_position(10,1.5f,20);expect_unheld();
            assert(original_calls==before && !prop_binding(actor)->remote_held);
            execute();assert(original_calls==before+1);
        } else if(mode==5) {
            expect_position(10,101.5f,20);assert(sst_prop_native_held(actor,player));
            assert(original_calls==before+1 && sends==1 && captured.phase==SST_PROP_CARRY);
        } else if(mode<=8) {
            expect_position(35,1.5f,45);assert(original_calls==before+1);
        } else {expect_position(500,1.5f,600);assert(original_calls==before+1);}
    }
    if(mode==2 || mode==3) {
        remote_scene=area;remote_presence=8;remote_connected=true;depart_tick();
        now_ms=1401;execute();assert(prop_binding(actor)->rest.ready);
        now_ms=1402;send_remote(1,SST_PROP_CARRY);execute();
        expect_position(10,1.5f,20);assert(!prop_binding(actor)->remote_held);
        // An old body's render receipt cannot lift the new incarnation's pot.
        displayed_presence=8;now_ms=1403;execute();expect_position(1000,101.5f,-1000);
        assert(prop_binding(actor)->remote_held && prop_binding(actor)->applied_generation==8);
    }
    if(mode!=5)assert(!sends);
    printf("profile%u local%u departure mode%u PASS\n",d->profile,local_player_id,mode);
    finish();
}
int main(int argc, char **argv) {
    assert(argc==2 || argc==4);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode>=1 && mode<=9);
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

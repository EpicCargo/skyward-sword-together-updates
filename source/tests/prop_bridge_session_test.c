// Reconnect completes before the next world tick; actual actor bridge, no game.
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main
static unsigned checks,failures;
#define CHECK(label,condition) do { checks++;if(!(condition)){failures++;fprintf(stderr,"FAIL %s\n",label);} } while(0)
static bool position(float x,float y,float z) {
    float p[3];memcpy(p,(void *)(uintptr_t)(actor+0x144),sizeof p);
    return fabsf(p[0]-x)<.001f && fabsf(p[1]-y)<.001f && fabsf(p[2]-z)<.001f;
}
static void pickup(void) {
    uint64_t backlink=arena_lo+0x7000;
    put64(player+0x1b90,actor);w32(actor+0x358,1);w32(actor+0x2e4,1);
    put64(actor+0x2d0,player);put64(actor+0x2d8,backlink);put64(backlink+0x10,actor);
    actual_pose(10,101.5f,20);
}
static void new_session(void) {
    network_generation=2;
    // The real renderer rejects its old connection's body receipt too.
    body_visible=false;
}
static void check(const SstPropNativeDescriptor *d,unsigned mode) {
    uint16_t identity=local_player_id;
    setup(d);execute();now_ms=1101;execute();now_ms=1102;
    if(mode==1) {
        send_remote(1,SST_PROP_PLACE);new_session();execute();
        CHECK("old session cannot apply a queued placement before world refresh",position(10,1.5f,20));
        CHECK("actor callback retires old session packet authority",!props_bridge.enabled && !props_bridge.state.initialized);
    } else if(mode==2) {
        send_remote(1,SST_PROP_PLACE);pickup();new_session();execute();
        CHECK("reconnect leaves genuine local held linkage intact",position(10,101.5f,20) && sst_prop_native_held(actor,player));
        CHECK("new session waits for bridge refresh before sending",!sends);
    } else if(mode==3) {
        send_remote(1,SST_PROP_CARRY);execute();expect_position(1000,101.5f,-1000);
        local.player_instance=2;actual_pose(35,1.5f,45);new_session();execute();
        CHECK("session cleanup cannot restore a previous player incarnation floor",position(35,1.5f,45));
    } else if(mode==4) {
        send_remote(1,SST_PROP_PLACE);execute();expect_position(500,1.5f,600);
        actual_pose(35,1.5f,45);send_remote(2,SST_PROP_PLACE);new_session();execute();
        CHECK("reconnect cannot rewind settled native simulation with an old placement",position(35,1.5f,45));
    } else if(mode==5) {
        send_remote(1,SST_PROP_PLACE);execute();
        CHECK("current session still applies a valid placement",position(500,1.5f,600) && props_bridge.enabled && props_bridge.state.initialized);
    } else if(mode==6) {
        send_remote(1,SST_PROP_PLACE);execute();new_session();unsigned before=original_calls;execute();
        CHECK("already applied placement remains on ordinary native path",position(500,1.5f,600) && original_calls==before+1);
    } else if(mode==7) {
        send_remote(1,SST_PROP_CARRY);execute();new_session();tick();execute();
        CHECK("fresh world session consumes outstanding floor cleanup",position(10,1.5f,20));
        remote_presence=displayed_presence=8;tick();now_ms=1301;send_remote(1,SST_PROP_PLACE);execute();
        SstProp current;
        CHECK("fresh authenticated session accepts its first restarted placement",position(500,1.5f,600) &&
            sst_props_get(&props_bridge.state,&prop_binding(actor)->key,now_ms,&current) && current.presence_generation==8 && current.sequence==1);
    } else if(mode==8) {
        send_remote(1,SST_PROP_CARRY);execute();new_session();w32(actor+8,43);pickup();
        local_player_id=identity==1?3:1;execute();
        CHECK("changed local role and actor retain native pickup",position(10,101.5f,20) && sst_prop_native_held(actor,player) && !sends);
    } else if(mode==9 || mode==10) {
        send_remote(1,SST_PROP_CARRY);execute();new_session();execute();
        CHECK("callback session retirement preserves valid floor cleanup",position(10,1.5f,20));
        if(mode==10) {
            now_ms=1110;actual_pose(35,1.5f,45);execute();
            CHECK("waiting for world refresh cannot repeat an old floor write",position(35,1.5f,45));
        }
    } else assert(!"unknown session mode");
    local_player_id=identity;
    printf("profile%u local%u session mode%u complete\n",d->profile,identity,mode);finish();
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==4);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode>=1 && mode<=10);
    if(argc==4) {
        const SstPropNativeDescriptor *d=sst_prop_native_descriptor((uint16_t)strtoul(argv[2],NULL,10));
        local_player_id=(uint16_t)strtoul(argv[3],NULL,10);assert(d && (local_player_id==1 || local_player_id==3));check(d,mode);
    } else for(unsigned i=0;i<SST_PROP_NATIVE_ADAPTERS;i++) {
        local_player_id=1;check(&sst_prop_native_descriptors[i],mode);local_player_id=3;check(&sst_prop_native_descriptors[i],mode);
    }
    printf("session: %u checks, %u failures; actual bridge, no game\n",checks,failures);
    return failures?1:0;
}

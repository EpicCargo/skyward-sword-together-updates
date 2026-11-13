// Local player/area changes before the next world tick; actual bridge, no game.
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main
static unsigned checks,failures,during_original;
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
static void context_original(Cpu *c) {
    original(c);
    if(during_original==9)local.player_instance=2;
    if(during_original==15)network_generation=2;
    if(during_original==16)local.scene.room=2;
}
static void context_execute(void) {
    cpu.x[0]=actor;cpu.x[1]=0x1234;prop_execute(&cpu,context_original);
    assert(cpu.x[0]==1 && cpu.x[1]==0x1234);
}
static void check(const SstPropNativeDescriptor *d,unsigned mode) {
    setup(d);during_original=0;execute();now_ms=1101;execute();now_ms=1102;
    if(mode<=6 || mode==12 || mode==14) {
        send_remote(1,SST_PROP_PLACE);actual_pose(35,1.5f,45);
        if(mode==1)strcpy(local.scene.stage,"F002");
        if(mode==2 || mode==12)local.scene.room=2;
        if(mode==3)local.scene.layer=2;
        if(mode==4)local.player_address=player+0x100;
        if(mode==5)local.player_instance=2;
        if(mode==6)local.valid=false;
        if(mode==14)local.scene.entrance++;
        execute();
        if(mode<=6) {
            CHECK("changed local context cannot apply old placement",position(35,1.5f,45));
            CHECK("changed local context retires packet authority",!props_bridge.enabled && !props_bridge.state.initialized);
        } else if(mode==12) {
            // World discovery authorizes the new area's native key, independently
            // of the stale binding discarded by the intervening callback.
            put8(actor+0x17c,2);remote_scene=local.scene;tick();
            pending=(SstProp){.scene=local.scene,.key=prop_binding(actor)->key,.from=2,
                .presence_generation=remote_presence,.sequence=2,.pose_tick=100,.phase=SST_PROP_PLACE,
                .received_ms=now_ms,.pos={80,1.5f,90},.matrix={1,0,0,80,0,1,0,1.5f,0,0,1,90}};
            assert(sst_prop_valid(&pending));have_pending=true;tick();execute();
            CHECK("fresh world context still admits a new placement",position(80,1.5f,90) && props_bridge.enabled);
        } else CHECK("entrance-only change preserves same-area placement",position(500,1.5f,600) && props_bridge.enabled);
    } else if(mode==7 || mode==13) {
        send_remote(1,SST_PROP_CARRY);execute();expect_position(1000,101.5f,-1000);
        if(mode==13)prop_tick(local,1,false,true);
        local.player_instance=2;actual_pose(35,1.5f,45);execute();
        CHECK("player reload cannot reuse an old carry or floor",position(35,1.5f,45));
    } else if(mode==8 || mode==9 || mode==11 || mode==15 || mode==16) {
        pickup();if(mode==8)local.player_instance=2;
        if(mode==9 || mode==15 || mode==16)during_original=mode;
        context_execute();
        CHECK("native held linkage survives context checks",position(10,101.5f,20) && sst_prop_native_held(actor,player));
        if(mode==11)CHECK("current local pickup still sends",sends==1 && captured.phase==SST_PROP_CARRY);
        else CHECK("context changed during native execution cannot send",!sends);
    } else if(mode==10) {
        send_remote(1,SST_PROP_PLACE);execute();
        CHECK("current local context still applies placement",position(500,1.5f,600) && props_bridge.enabled);
    } else assert(!"unknown context mode");
    printf("profile%u local%u context mode%u complete\n",d->profile,local_player_id,mode);finish();
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==4);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode>=1 && mode<=16);
    if(argc==4) {
        const SstPropNativeDescriptor *d=sst_prop_native_descriptor((uint16_t)strtoul(argv[2],NULL,10));
        local_player_id=(uint16_t)strtoul(argv[3],NULL,10);assert(d && (local_player_id==1 || local_player_id==3));check(d,mode);
    } else for(unsigned i=0;i<SST_PROP_NATIVE_ADAPTERS;i++) {
        local_player_id=1;check(&sst_prop_native_descriptors[i],mode);local_player_id=3;check(&sst_prop_native_descriptors[i],mode);
    }
    printf("context: %u checks, %u failures; actual bridge, no game\n",checks,failures);
    return failures?1:0;
}

// Remote roster changes before periodic discovery; real bridge, no game/GPU.
#define SST_PROP_FIXTURE_CUSTOM_ROSTER
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main
static unsigned checks,failures;
static unsigned owner_rounds=1;
static bool extra_peer,duplicate_peer;
#define CHECK(label,condition) do { checks++;if(!(condition)){failures++;fprintf(stderr,"FAIL %s\n",label);} } while(0)
int sst_net_players(SstPlayer *out,int capacity) {
    int n=0;assert(capacity>=3);
    if(remote_connected)out[n++]=(SstPlayer){.active=true,.id=2,.presence_generation=remote_presence,.scene=remote_scene};
    if(extra_peer)out[n++]=(SstPlayer){.active=true,.id=4,.presence_generation=17,.scene=area};
    if(duplicate_peer)out[n++]=(SstPlayer){.active=true,.id=2,.presence_generation=remote_presence,.scene=remote_scene};
    return n;
}
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
static void retire(unsigned kind) {
    if(kind==1)remote_connected=false;
    else if(kind==2)remote_presence=8;
    else remote_scene.room=2;
}
static void check(const SstPropNativeDescriptor *d,unsigned mode) {
    uint16_t identity=local_player_id;extra_peer=duplicate_peer=false;
    setup(d);execute();now_ms=1101;execute();now_ms=1102;
    if(mode<=3 || mode==9 || mode==12 || mode==14) {
        send_remote(1,SST_PROP_PLACE);
        if(mode<=3)retire(mode);
        if(mode==12)retire(2);
        if(mode==14)retire(1);
        execute();
        if(mode<=3)CHECK("retired peer cannot apply a queued placement before world refresh",position(10,1.5f,20));
        if(mode==9)CHECK("current peer still applies a placement",position(500,1.5f,600));
        if(mode==12) {
            displayed_presence=8;tick();send_remote(1,SST_PROP_PLACE);execute();
            CHECK("new incarnation accepts its restarted placement after refresh",position(500,1.5f,600) && prop_binding(actor)->applied_generation==8);
        }
        if(mode==14) {
            SstProp old=pending;old.sequence=999;old.received_ms=now_ms;
            uint64_t before=sst_prop_presentation_stats.received;prop_incoming(&old,NULL);
            CHECK("callback retirement rejects late packets from the old tuple",sst_prop_presentation_stats.received==before);
        }
    } else if(mode==7 || mode==11) {
        send_remote(1,SST_PROP_PLACE);execute();expect_position(500,1.5f,600);
        if(mode==7){actual_pose(35,1.5f,45);send_remote(2,SST_PROP_PLACE);}
        retire(1);execute();
        CHECK("departure cannot rewind an already settled native placement",mode==7?position(35,1.5f,45):position(500,1.5f,600));
    } else if(mode==13) {
        // A second live native key belongs to a different authenticated peer.
        uint64_t second=arena_lo+0xa000;
        memcpy((void *)(uintptr_t)second,(void *)(uintptr_t)actor,d->allocation);
        w32(second+8,43);put16(second+0x136,10);
        put64(second+0x18,player+0x18);put64(second+0x20,0);put64(second+0x30,0);put64(second+0x38,second);
        put64(actor+0x30,second+0x18);extra_peer=true;tick();now_ms++;
        send_remote(1,SST_PROP_PLACE);
        SstProp other=pending;other.key=prop_binding(second)->key;other.from=4;other.presence_generation=17;
        other.received_ms=now_ms;prop_incoming(&other,NULL);
        uint64_t first=actor;actor=second;execute();actual_pose(35,1.5f,45);actor=first;
        for(unsigned i=0;i<owner_rounds;i++) {
            if(i) {
                now_ms+=10;remote_connected=true;remote_presence++;execute();
                SstProp fresh=pending;fresh.presence_generation=remote_presence;
                fresh.received_ms=now_ms;prop_incoming(&fresh,NULL);
            }
            retire(1);execute();
            if(owner_rounds>1) {
                SstProp missing;SstPropKey first_key=prop_binding(first)->key;
                CHECK("repeated remote retirement leaves no old placement authority",position(10,1.5f,20) && !sst_props_get(&props_bridge.state,&first_key,now_ms,&missing));
                actor=second;execute();
                CHECK("stable native placement stamp survives other peer incarnations",position(35,1.5f,45));actor=first;
            }
        }
        SstProp found;
        CHECK("one departing peer preserves another live key and sender",sst_props_get(&props_bridge.state,&other.key,now_ms,&found) && found.from==4 && found.sequence==1);
        uint64_t before=sst_prop_presentation_stats.received;prop_incoming(&other,NULL);
        CHECK("stable peer retains replay watermarks across callback roster refresh",sst_prop_presentation_stats.received==before);
    } else {
        send_remote(1,SST_PROP_CARRY);execute();expect_position(1000,101.5f,-1000);
        if(mode>=4 && mode<=6)retire(mode-3);
        if(mode==8 || mode==17){pickup();retire(1);}
        if(mode==15)local_player_id=0;
        if(mode==16 || mode==18)duplicate_peer=true;
        if(mode==18)tick();
        execute();
        if((mode>=4 && mode<=6) || mode>=15) {
            if(mode==17)CHECK("native pickup can send after the old remote owner retires",sends==1 && captured.phase==SST_PROP_CARRY);
            else {
                CHECK("retired remote carry returns to the saved floor before world refresh",position(10,1.5f,20));
                if(mode==15 || mode==16)CHECK("invalid callback roster disables packet authority",!props_bridge.enabled && !props_bridge.state.initialized);
                actual_pose(35,1.5f,45);execute();
                CHECK("remote retirement cannot repeat an old floor write",position(35,1.5f,45));
            }
        } else if(mode==8)CHECK("peer departure preserves genuine native held linkage",position(10,101.5f,20) && sst_prop_native_held(actor,player));
        else if(mode==10)CHECK("current peer retains normal moving carry",position(1000,101.5f,-1000) && prop_binding(actor)->remote_held);
        else assert(!"unknown owner mode");
    }
    local_player_id=identity;
    printf("profile%u local%u owner mode%u complete\n",d->profile,identity,mode);finish();
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==4 || argc==5);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode>=1 && mode<=18);
    if(argc==5){owner_rounds=(unsigned)strtoul(argv[4],NULL,10);assert(mode==13 && owner_rounds && owner_rounds<=100000);}
    if(argc>=4) {
        const SstPropNativeDescriptor *d=sst_prop_native_descriptor((uint16_t)strtoul(argv[2],NULL,10));
        local_player_id=(uint16_t)strtoul(argv[3],NULL,10);assert(d && (local_player_id==1 || local_player_id==3));check(d,mode);
    } else for(unsigned i=0;i<SST_PROP_NATIVE_ADAPTERS;i++) {
        local_player_id=1;check(&sst_prop_native_descriptors[i],mode);local_player_id=3;check(&sst_prop_native_descriptors[i],mode);
    }
    printf("owner: %u checks, %u failures; actual bridge, no game\n",checks,failures);
    return failures?1:0;
}

// Actual prop bridge over a synthetic native arena. No game/GPU or sockets.
#define SST_PROP_FIXTURE_ARENA_SIZE 0x100000
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main

static unsigned checks, failures;
static unsigned churn_rounds=1000;
#define CHECK(label, condition) do { checks++; if (!(condition)) { failures++; fprintf(stderr,"FAIL %s\n",label); } } while (0)
static SstPropKey key;
static SstProp packet(uint16_t id, uint32_t sequence, SstPropPhase phase, uint64_t arrival) {
    float x=phase==SST_PROP_CARRY?100:500,y=phase==SST_PROP_CARRY?101.5f:1.5f,z=phase==SST_PROP_CARRY?-100:600;
    SstProp p={.scene=area,.key=key,.from=2,.presence_generation=7,.sequence=sequence,
        .pose_tick=100,.phase=phase,.received_ms=arrival,.origin={100,1.5f,-100},
        .pos={x,y,z},.matrix={1,0,0,x,0,1,0,y,0,0,1,z}};
    p.key.placed_id=id;assert(sst_prop_valid(&p));return p;
}
static void incoming(uint16_t id, uint32_t sequence, SstPropPhase phase, uint64_t arrival) {
    SstProp p=packet(id,sequence,phase,arrival);prop_incoming(&p,NULL);
}
static bool position(float x,float y,float z) {
    float value[3];memcpy(value,(void *)(uintptr_t)(actor+0x144),sizeof value);
    return fabsf(value[0]-x)<.001f && fabsf(value[1]-y)<.001f && fabsf(value[2]-z)<.001f;
}
static unsigned tracks(void) {
    unsigned n=0;for(unsigned i=0;i<SST_PROPS;i++)n+=props_bridge.state.props[i].used;return n;
}
static bool target_sequence(uint32_t sequence) {
    SstProp out;
    return sst_props_get(&props_bridge.state,&key,now_ms,&out) && out.sequence==sequence && out.from==2;
}
static void replace(uint16_t id, uint32_t instance) {
    w32(actor+8,instance);put16(actor+0x136,id);actual_pose(35,1.5f,45);tick();
    key=prop_binding(actor)->key;
}
static void pickup(void) {
    uint64_t backlink=arena_lo+0x7000;
    put64(player+0x1b90,actor);w32(actor+0x358,1);w32(actor+0x2e4,1);
    put64(actor+0x2d0,player);put64(actor+0x2d8,backlink);put64(backlink+0x10,actor);
    actual_pose(10,101.5f,20);
}
static void full_table(const SstPropNativeDescriptor *d) {
    uint64_t last=0;
    for(unsigned i=0;i<SST_PROPS-1;i++) {
        uint64_t next=arena_lo+0x10000+(uint64_t)i*0x2000;
        memcpy((void *)(uintptr_t)next,(void *)(uintptr_t)actor,d->allocation);
        w32(next+8,100+i);put16(next+0x136,(uint16_t)(10+i));
        put64(next+0x18,player+0x18);put64(next+0x20,0);put64(next+0x30,0);put64(next+0x38,next);
        put64((last?last:actor)+0x30,next+0x18);last=next;
    }
    now_ms=1200;tick();now_ms=1201;
    for(unsigned i=0;i<SST_PROPS;i++)incoming((uint16_t)(9+i),1,SST_PROP_PLACE,now_ms);
    assert(tracks()==SST_PROPS);execute();actual_pose(35,1.5f,45);
    uint16_t latest=0;
    for(unsigned i=0;i<churn_rounds;i++) {
        latest=(uint16_t)(100+i%1000);now_ms=1300+(uint64_t)i*10;
        w32(last+8,1000+i);put16(last+0x136,latest);tick();now_ms++;
        incoming(latest,2+i,SST_PROP_PLACE,now_ms);
    }
    SstPropKey changed=key;changed.placed_id=latest;SstProp out;
    CHECK("full native table admits current replacement key",sst_props_get(&props_bridge.state,&changed,now_ms,&out) && out.sequence==churn_rounds+1);
    bool stable=tracks()==SST_PROPS;
    for(unsigned i=0;i<SST_PROPS-1;i++) {
        SstPropKey unchanged=key;unchanged.placed_id=(uint16_t)(9+i);
        stable=stable && sst_props_get(&props_bridge.state,&unchanged,now_ms,&out) && out.sequence==1;
    }
    CHECK("full native table preserves every stable placement",stable);
    execute();CHECK("full native churn preserves stable presentation stamp",position(35,1.5f,45));
}
static void check(const SstPropNativeDescriptor *d,unsigned mode) {
    setup(d);key=prop_binding(actor)->key;execute();now_ms=1101;execute();now_ms=1102;
    if(mode==1) {
        for(unsigned i=0;i<SST_PROPS;i++)incoming((uint16_t)(100+i),1,SST_PROP_PLACE,now_ms);
        CHECK("unbound packets do not consume native target capacity",tracks()==0 && !sst_prop_presentation_stats.received);
        incoming(9,2,SST_PROP_CARRY,now_ms);execute();
        CHECK("bound carry survives unbound packet traffic",position(1000,101.5f,-1000));
    } else if(mode==2 || mode==3 || mode==12) {
        incoming(9,1,SST_PROP_PLACE,now_ms);execute();assert(position(500,1.5f,600));
        now_ms=1300;
        if(mode==12) {
            w32(actor+8,43);actual_pose(35,1.5f,45);
            uint64_t received=sst_prop_presentation_stats.received;
            incoming(9,100,SST_PROP_PLACE,now_ms);
            CHECK("changed native identity cannot admit packets before rebind",sst_prop_presentation_stats.received==received);
            now_ms=1400;tick();
        } else {
            if(mode==3) {pending=packet(9,100,SST_PROP_PLACE,now_ms);have_pending=true;}
            replace(9,43);
        }
        execute();
        const char *label=mode==2?"accepted PLACE cannot cross a native actor lifetime":
            mode==3?"queued pre-bind PLACE cannot cross a native actor lifetime":"changed native identity cannot cache a future placement";
        CHECK(label,position(35,1.5f,45));
        if(mode==2) {
            uint64_t received=sst_prop_presentation_stats.received;
            now_ms++;incoming(9,1,SST_PROP_PLACE,now_ms);
            CHECK("replacement preserves retired-key replay watermark",sst_prop_presentation_stats.received==received);
        }
        // An old queued packet must not poison the new lifetime's watermark.
        now_ms++;incoming(9,2,SST_PROP_PLACE,now_ms);execute();
        CHECK("fresh post-bind placement remains admissible",position(500,1.5f,600) && target_sequence(2));
    } else if(mode==4) {
        for(unsigned i=0;i<40;i++) {
            now_ms=1300+i*10;replace((uint16_t)(10+i),100+i);
            now_ms++;incoming((uint16_t)(10+i),1,SST_PROP_PLACE,now_ms);execute();
        }
        CHECK("retired native keys release target capacity",tracks()==1);
        CHECK("new native key remains usable after forty replacements",position(500,1.5f,600));
    } else if(mode==5) {
        incoming(9,1,SST_PROP_PLACE,now_ms);execute();assert(position(500,1.5f,600));
        now_ms=1200;put64(player+0x20,0);tick();
        incoming(9,100,SST_PROP_PLACE,now_ms);
        CHECK("missing native actors cannot retain or acquire targets",tracks()==0);
        now_ms=1400;put64(player+0x20,actor+0x18);replace(9,43);execute();
        CHECK("reappearing native actor ignores an absent-lifetime placement",position(35,1.5f,45));
        now_ms=1401;incoming(9,2,SST_PROP_PLACE,now_ms);execute();
        CHECK("reappearing native actor accepts a new placement",position(500,1.5f,600) && target_sequence(2));
    } else if(mode==6) {
        // First discovery can legitimately follow an already received packet.
        incoming(9,1,SST_PROP_PLACE,999);execute();
        CHECK("initial binding accepts a fresh earlier arrival",position(500,1.5f,600));
    } else if(mode==7) {
        incoming(9,1,SST_PROP_PLACE,now_ms);execute();
        uint64_t second=arena_lo+0xa000;
        memcpy((void *)(uintptr_t)second,(void *)(uintptr_t)actor,d->allocation);
        w32(second+8,99);put16(second+0x136,10);put64(second+0x18,player+0x18);
        put64(second+0x20,0);put64(second+0x30,0);put64(second+0x38,second);
        put64(actor+0x30,second+0x18);now_ms=1200;tick();
        actual_pose(35,1.5f,45);w32(second+8,100);put16(second+0x136,11);now_ms=1300;tick();
        SstProp prior={0};
        CHECK("unrelated actor replacement preserves stable target",sst_props_get(&props_bridge.state,&key,now_ms,&prior) && prior.sequence==1);
        CHECK("unrelated actor replacement preserves replay watermark",!sst_props_update(&props_bridge.state,2,7,&prior,now_ms));
        execute();CHECK("stable placement stamp is not reapplied",position(35,1.5f,45));
    } else if(mode==8) {
        incoming(9,1,SST_PROP_PLACE,now_ms);execute();now_ms=1300;replace(9,43);pickup();
        execute();
        CHECK("new lifetime local pickup retains native ownership",position(10,101.5f,20) && sst_prop_native_held(actor,player) && sends==1 && captured.phase==SST_PROP_CARRY);
    } else if(mode==9) {
        incoming(9,1,SST_PROP_CARRY,now_ms);execute();now_ms=1300;replace(9,43);execute();
        CHECK("new carry waits for its own stationary native floor",position(35,1.5f,45));
        now_ms=1301;incoming(9,2,SST_PROP_CARRY,now_ms);execute();
        now_ms=1402;execute();CHECK("fresh carry can acquire a replacement actor",position(1000,101.5f,-1000));
        now_ms=3000;execute();CHECK("replacement carry restores its own floor",position(35,1.5f,45));
    } else if(mode==10) {
        incoming(9,1,SST_PROP_CARRY,now_ms);execute();
        now_ms=1200;put64(actor+0x30,actor+0x18);tick();execute();
        CHECK("discovery failure still restores an outstanding carry",position(10,1.5f,20));
        put64(actor+0x30,0);tick();now_ms=1400;execute();now_ms=1501;execute();
        now_ms=1502;incoming(9,2,SST_PROP_CARRY,now_ms);execute();
        CHECK("valid discovery can resume a stable actor carry",position(1000,101.5f,-1000));
    } else if(mode==11) {
        full_table(d);
    } else assert(!"unknown admission mode");
    printf("profile%u local%u admission mode%u complete\n",d->profile,local_player_id,mode);finish();
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==4 || argc==5);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode>=1 && mode<=12);
    if(argc==5) {churn_rounds=(unsigned)strtoul(argv[4],NULL,10);assert(mode==11 && churn_rounds>=1 && churn_rounds<=1000000);}
    if(argc>=4) {
        const SstPropNativeDescriptor *d=sst_prop_native_descriptor((uint16_t)strtoul(argv[2],NULL,10));
        local_player_id=(uint16_t)strtoul(argv[3],NULL,10);assert(d && (local_player_id==1 || local_player_id==3));check(d,mode);
    } else for(unsigned i=0;i<SST_PROP_NATIVE_ADAPTERS;i++) {
        local_player_id=1;check(&sst_prop_native_descriptors[i],mode);local_player_id=3;check(&sst_prop_native_descriptors[i],mode);
    }
    printf("admission: %u checks, %u failures; actual bridge, no game\n",checks,failures);
    return failures?1:0;
}

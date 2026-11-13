// Interrupt each real model-call boundary over a synthetic native arena.
#define SST_PROP_FIXTURE_CUSTOM_GUEST_CALL
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main
static unsigned checks,failures,selected_mode,selected_stage;
static bool armed,interrupted;
static unsigned char boundary[0x10000];
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
void guest_call(Cpu *c,uint64_t address) {
    assert(pthread_mutex_trylock(&props_bridge.mu)==0);pthread_mutex_unlock(&props_bridge.mu);
    assert(c->x[0]==model);
    if(address==SST_PROP_MODEL_OPTIONS)assert(c->x[1]==2 && c->x[2]<=1);
    else assert(address==SST_PROP_MODEL_PROCESS && c->x[1]==1 && !c->x[2] && !c->x[3]);
    model_calls++;c->x[1]=0;
    if(!armed || model_calls!=selected_stage)return;
    interrupted=true;
    if(selected_mode==1){w32(actor+8,43);actual_pose(35,1.5f,45);}
    if(selected_mode==2){uint64_t next=arena_lo+0xc000;put64(next,SST_PROP_MODEL_VTABLE);put64(actor+0x438,next);actual_pose(35,1.5f,45);}
    if(selected_mode==3)pickup();
    if(selected_mode==4){put16(actor+0x136,10);actual_pose(35,1.5f,45);}
    if(selected_mode==5){local.player_instance=2;actual_pose(35,1.5f,45);}
    if(selected_mode==6){local.scene.room=2;actual_pose(35,1.5f,45);}
    if(selected_mode==7){network_generation=2;body_visible=false;}
    if(selected_mode==8)g_sst.sync_props=false;
    if(selected_mode==9)remote_connected=false;
    if(selected_mode==10)remote_presence=8;
    if(selected_mode==12)put64(model,0);
    if(selected_mode==13)prop_tick(local,1,false,true);
    if(selected_mode==14) {
        SstProp next=pending;next.sequence=2;next.phase=SST_PROP_PLACE;
        next.pos[0]=next.matrix[3]=700;next.pos[1]=next.matrix[7]=1.5f;
        next.pos[2]=next.matrix[11]=800;prop_incoming(&next,NULL);
    }
    memcpy(boundary,arena,sizeof boundary);
}
static void check(const SstPropNativeDescriptor *d,unsigned mode,unsigned stage) {
    g_sst.sync_props=true;setup(d);selected_mode=mode;selected_stage=stage;armed=interrupted=false;
    execute();now_ms=1101;execute();now_ms=1102;send_remote(1,SST_PROP_CARRY);
    unsigned originals=original_calls;armed=true;execute();armed=false;
    CHECK("model presentation never reexecutes a partially handled actor",original_calls==originals);
    if(stage && mode!=11) {
        CHECK("interrupted model tail cannot write into the changed context",interrupted && !memcmp(boundary,arena,sizeof boundary));
        CHECK("interrupted model presentation stops further guest calls",model_calls==stage);
        CHECK("interrupted model presentation cannot commit applied statistics",!sst_prop_presentation_stats.carried_presented);
        if((mode>=7 && mode<=10) || mode==13) {
            execute();CHECK("partially presented carry retains once-only valid floor cleanup",position(10,1.5f,20));
            actual_pose(35,1.5f,45);execute();CHECK("partial carry cleanup cannot rewind later native simulation",position(35,1.5f,45));
        }
        if(mode==14) {
            execute();CHECK("newer placement survives interrupted carry presentation",position(700,1.5f,800) && prop_binding(actor)->applied_sequence==2);
            actual_pose(35,1.5f,45);execute();CHECK("committed newer placement cannot rewind native simulation",position(35,1.5f,45));
        }
    } else CHECK("unchanged model and no-call barrel retain ordinary carry",position(1000,101.5f,-1000) && sst_prop_presentation_stats.carried_presented==1 && model_calls==(stage?3u:0u));
    g_sst.sync_props=true;
    printf("profile%u local%u model mode%u stage%u complete\n",d->profile,local_player_id,mode,stage);finish();
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==5);unsigned mode=(unsigned)strtoul(argv[1],NULL,10);assert(mode>=1 && mode<=14);
    if(argc==5) {
        const SstPropNativeDescriptor *d=sst_prop_native_descriptor((uint16_t)strtoul(argv[2],NULL,10));
        local_player_id=(uint16_t)strtoul(argv[3],NULL,10);unsigned stage=(unsigned)strtoul(argv[4],NULL,10);
        assert(d && (local_player_id==1 || local_player_id==3) && stage<=3 && (stage!=0)==!!(d->tail_flags&SST_PROP_NATIVE_CALC_TASKS));check(d,mode,stage);
    } else for(unsigned i=0;i<SST_PROP_NATIVE_ADAPTERS;i++) {
        const SstPropNativeDescriptor *d=&sst_prop_native_descriptors[i];
        for(unsigned role=0;role<2;role++)for(unsigned stage=1;stage<=(d->tail_flags&SST_PROP_NATIVE_CALC_TASKS?3u:1u);stage++) {
            local_player_id=role?3:1;check(d,mode,d->tail_flags&SST_PROP_NATIVE_CALC_TASKS?stage:0);
        }
    }
    printf("model: %u checks, %u failures; actual bridge, no game\n",checks,failures);return failures?1:0;
}

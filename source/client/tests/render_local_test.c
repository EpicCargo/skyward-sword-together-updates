#include "../src/sst_game.h"
#include "../src/sst_animation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char actors[2][384],file_mgr[16];
    uint64_t player,file_mgr_address;
    char stage[16];
    unsigned char room,layer,entrance;
    SstGameLayout layout;
} Fixture;
static const float identity[12]={1,0,0,0,0,1,0,0,0,0,1,0};
static void put(void *base,size_t offset,const void *value,size_t size) {
    memcpy((unsigned char *)base+offset,value,size);
}
static void animation(Fixture *f,unsigned index,uint16_t clip,float phase,float length,uint32_t action) {
    SstGameLayout *l=&f->layout;
    unsigned char selected=(unsigned char)index;
    put(f->actors[0],l->off_anim_index,&selected,1);
    put(f->actors[0],l->off_action,&action,4);
    if(index<6) {
        put(f->actors[0],l->off_anim_ids+index*2,&clip,2);
        put(f->actors[0],l->off_anim_tracks+index*l->anim_track_stride+l->off_anim_frame,&phase,4);
        put(f->actors[0],l->off_anim_tracks+index*l->anim_track_stride+l->off_anim_length,&length,4);
    }
}
static void initialize(Fixture *f) {
    memset(f,0,sizeof *f);
    f->layout=(SstGameLayout){
        .player_ptr=(uint64_t)(uintptr_t)&f->player,.stage_name=(uint64_t)(uintptr_t)f->stage,
        .room=(uint64_t)(uintptr_t)&f->room,.layer=(uint64_t)(uintptr_t)&f->layer,
        .entrance=(uint64_t)(uintptr_t)&f->entrance,
        .file_mgr_ptr=(uint64_t)(uintptr_t)&f->file_mgr_address,
        .guest_lo=(uint64_t)(uintptr_t)f,.guest_hi=(uint64_t)(uintptr_t)f+sizeof *f,
        .off_pos=32,.off_rot=48,.off_action=64,.off_player_instance=8,
        .off_anim_index=68,.off_anim_ids=72,.off_anim_tracks=96,
        .anim_track_stride=32,.off_anim_frame=4,.off_anim_length=8,
        .off_health=2,.off_max_health=4
    };
    f->player=(uint64_t)(uintptr_t)f->actors[0];
    f->file_mgr_address=(uint64_t)(uintptr_t)f->file_mgr;
    memcpy(f->stage,"F001r",6);f->room=1;f->layer=2;f->entrance=3;
    uint32_t instance=17;put(f->actors[0],8,&instance,4);
    float pos[3]={100,10,-400};put(f->actors[0],32,pos,sizeof pos);
    int16_t rot[3]={0,0,0};put(f->actors[0],48,rot,sizeof rot);
    uint16_t health=24;put(f->file_mgr,2,&health,2);put(f->file_mgr,4,&health,2);
    animation(f,2,26,9,45,0);
    memcpy(f->actors[1],f->actors[0],sizeof f->actors[0]);
}
static void zero_local(SstLocal local) {
    SstLocal zero={0};
    assert(!local.valid && !local.player_address && !local.player_instance);
    assert(!memcmp(&local.scene,&zero.scene,sizeof local.scene));
    assert(!local.pose.tick && !local.pose.anim_id && !local.pose.action && !local.animation_length);
    for(unsigned i=0;i<3;i++)assert(!local.pose.pos[i] && !local.pose.rot[i]);
    assert(!local.pose.anim_frame && !local.pose.health && !local.pose.max_health && !local.pose.equip_flags);
}
static void environment(const char *value) {
#ifdef _WIN32
    assert(!_putenv_s("SST_NO_PLAYER_READ",value?value:""));
#else
    assert(!(value?setenv("SST_NO_PLAYER_READ",value,1):unsetenv("SST_NO_PLAYER_READ")));
#endif
}
static void guards_before_native_reads(void) {
    SstGameLayout inaccessible={.player_ptr=1};SstLocal published={0};
    zero_local(sst_game_read_render_local(NULL,&published,17));
    zero_local(sst_game_read_render_local(&inaccessible,NULL,17));
    zero_local(sst_game_read_render_local(&inaccessible,&published,17));
    published.valid=true;published.player_address=42;
    environment("1");zero_local(sst_game_read_render_local(&inaccessible,&published,17));
    // Presence, even the string"0", suppresses reads under the existing flag contract.
    environment("0");zero_local(sst_game_read_render_local(&inaccessible,&published,17));
    environment(NULL);
}
static void fresh_metadata_and_clock(void) {
    Fixture f;initialize(&f);
    uint32_t clock_before=sst_game_pose_tick();SstLocal global_before=sst_game_local();
    SstLocal published=sst_game_read_local(&f.layout,7),saved;
    assert(published.valid);memcpy(&saved,&published,sizeof saved);
    published.pose.equip_flags=0xff;memcpy(&saved,&published,sizeof saved);
    float pos[3]={107.5f,11,-399};int16_t rot[3]={1,1234,2};
    put(f.actors[0],32,pos,sizeof pos);put(f.actors[0],48,rot,sizeof rot);
    animation(&f,2,26,10,45,0);
    SstLocal render=sst_game_read_render_local(&f.layout,&published,7);
    assert(render.valid && render.pose.tick==7 && render.pose.anim_frame==10);
    assert(!memcmp(render.pose.pos,pos,sizeof pos) && !memcmp(render.pose.rot,rot,sizeof rot));
    assert(render.animation_length==45 && render.pose.equip_flags==0);
    assert(!memcmp(&published,&saved,sizeof saved)); // no caller/publisher mutation
    animation(&f,4,20,3.5f,24,3);
    render=sst_game_read_render_local(&f.layout,&published,7);
    assert(render.valid && render.pose.anim_id==20 && render.pose.action==3);
    assert(render.pose.anim_frame==3.5f && render.animation_length==24 && render.pose.tick==7);
    for(unsigned i=0;i<3;i++)assert(sst_game_read_render_local(&f.layout,&published,7).pose.anim_id==20);
    assert(sst_game_pose_tick()==clock_before);
    SstLocal global_after=sst_game_local();
    assert(global_after.valid==global_before.valid && global_after.player_address==global_before.player_address);
    assert(global_after.player_instance==global_before.player_instance && global_after.pose.tick==global_before.pose.tick);
    assert(!memcmp(&global_after.scene,&global_before.scene,sizeof global_after.scene));
    assert(!memcmp(global_after.pose.pos,global_before.pose.pos,sizeof global_after.pose.pos));
}
static void transitions_and_invalid_roots(void) {
    Fixture f;initialize(&f);SstLocal published=sst_game_read_local(&f.layout,9);
    f.player=0;zero_local(sst_game_read_render_local(&f.layout,&published,9));
    f.player=1;zero_local(sst_game_read_render_local(&f.layout,&published,9));
    f.player=UINT64_MAX-4;zero_local(sst_game_read_render_local(&f.layout,&published,9));
    f.player=(uint64_t)(uintptr_t)f.actors[1];zero_local(sst_game_read_render_local(&f.layout,&published,9));
    f.player=published.player_address;
    uint32_t instance=18;put(f.actors[0],8,&instance,4);
    zero_local(sst_game_read_render_local(&f.layout,&published,9));
    // Once publication observes replacement, the fresh render read is permitted.
    SstLocal replacement=sst_game_read_local(&f.layout,10);
    assert(sst_game_read_render_local(&f.layout,&replacement,10).valid);
    instance=published.player_instance;put(f.actors[0],8,&instance,4);
    instance=0;put(f.actors[0],8,&instance,4);replacement=sst_game_read_local(&f.layout,10);
    assert(sst_game_read_render_local(&f.layout,&replacement,10).valid); // valid instance0
    instance=published.player_instance;put(f.actors[0],8,&instance,4);
    f.room++;zero_local(sst_game_read_render_local(&f.layout,&published,9));f.room--;
    f.layer++;zero_local(sst_game_read_render_local(&f.layout,&published,9));f.layer--;
    f.entrance++;zero_local(sst_game_read_render_local(&f.layout,&published,9));f.entrance--;
    memcpy(f.stage,"D000",5);zero_local(sst_game_read_render_local(&f.layout,&published,9));
    f.stage[0]=0;zero_local(sst_game_read_render_local(&f.layout,&published,9));
    f.stage[0]=1;zero_local(sst_game_read_render_local(&f.layout,&published,9));
    memcpy(f.stage,"F001r",6);
    float invalid=NAN;put(f.actors[0],32,&invalid,4);
    zero_local(sst_game_read_render_local(&f.layout,&published,9));
    invalid=1000001;put(f.actors[0],32,&invalid,4);
    zero_local(sst_game_read_render_local(&f.layout,&published,9));
    initialize(&f);published=sst_game_read_local(&f.layout,9);
    // Both title exclusions must fail despite a matching published identity and
    // retained valid health. Construct the published scene manually for this guard.
    memcpy(f.stage,"F000",5);f.room=0;f.layer=28;f.entrance=48;
    memset(published.scene.stage,0,sizeof published.scene.stage);memcpy(published.scene.stage,"F000",5);
    published.scene.room=0;published.scene.layer=28;published.scene.entrance=48;
    zero_local(sst_game_read_render_local(&f.layout,&published,9));
    f.layer=26;f.entrance=55;published.scene.layer=26;published.scene.entrance=55;
    zero_local(sst_game_read_render_local(&f.layout,&published,9));
}
static void optional_fields_remain_untrusted(void) {
    Fixture f;initialize(&f);SstLocal published=sst_game_read_local(&f.layout,9),render;
    const float bad_phases[]={NAN,INFINITY,-1,65537};
    for(unsigned i=0;i<sizeof bad_phases/sizeof *bad_phases;i++) {
        animation(&f,2,26,bad_phases[i],45,0);
        render=sst_game_read_render_local(&f.layout,&published,9);
        assert(render.valid && render.pose.anim_id==UINT16_MAX && render.pose.anim_frame==0);
        assert(render.animation_length==0 && render.pose.anim_id!=published.pose.anim_id);
    }
    animation(&f,6,26,9,45,0);render=sst_game_read_render_local(&f.layout,&published,9);
    assert(render.valid && render.pose.anim_id==UINT16_MAX);
    animation(&f,2,443,9,45,0);render=sst_game_read_render_local(&f.layout,&published,9);
    assert(render.valid && render.pose.anim_id==UINT16_MAX);
    f.layout.off_anim_ids=0;render=sst_game_read_render_local(&f.layout,&published,9);
    assert(render.valid && render.pose.anim_id==UINT16_MAX);
    f.layout.off_anim_ids=72;
    animation(&f,2,26,9,NAN,0);render=sst_game_read_render_local(&f.layout,&published,9);
    assert(render.valid && render.pose.anim_id==26 && render.pose.anim_frame==9 && render.animation_length==0);
    uint16_t corrupt_health=81;put(f.file_mgr,4,&corrupt_health,2);
    render=sst_game_read_render_local(&f.layout,&published,9);
    assert(render.valid && !render.pose.health && !render.pose.max_health); // existing renderer health gate
    f.file_mgr_address=0;render=sst_game_read_render_local(&f.layout,&published,9);
    assert(render.valid && !render.pose.max_health); // no fallback to published24
}
static float canonical_x(const SstLocal *metadata,const float model[12],const float bone[12]) {
    sst_animation_reset();SstPose local=metadata->pose;
    for(unsigned i=0;i<=6;i++) {
        local.anim_frame=3+(float)i;
        sst_animation_frame(&metadata->scene,&local,(uint64_t)i);
        assert(sst_animation_set_length(local.anim_id,45));
    }
    assert(sst_animation_capture(1,&local,identity,model,bone,48,NULL,0));
    SstPose remote={.anim_id=26,.anim_frame=9,.action=0};SstAnimationPose selected;
    assert(sst_animation_select(1,&remote,48,0,&selected));
    float out[12];assert(sst_animation_uniform(&selected,&remote,identity,3,out,48));
    float x=out[3];sst_animation_reset();return x;
}
static void actual_canonicalization_effect(void) {
    Fixture f;initialize(&f);SstLocal published=sst_game_read_local(&f.layout,7);
    float x=107.5f;put(f.actors[0],32,&x,4);
    SstLocal fresh=sst_game_read_render_local(&f.layout,&published,7);assert(fresh.valid);
    float model[12],bone[12];memcpy(model,identity,48);
    model[3]=fresh.pose.pos[0];model[7]=fresh.pose.pos[1];model[11]=fresh.pose.pos[2];
    memcpy(bone,model,48);bone[7]+=200;
    float stale_x=canonical_x(&published,model,bone),fresh_x=canonical_x(&fresh,model,bone);
    assert(fabsf(stale_x-7.5f)<.001f && fabsf(fresh_x)<.001f);
    printf("current native palette canonicalization: cached-root residual=%.3f fresh-root residual=%.3f\n",stale_x,fresh_x);
}
int main(void) {
    if(getenv("SST_TEST_RIGID_DROP_FILE") && getenv("LARECOMP_DEBUG_DATA")) {
        fputs("Unset SST_TEST_RIGID_DROP_FILE before this pure fixture.\n",stderr);return 2;
    }
    const char *inherited=getenv("SST_NO_PLAYER_READ");char *saved=NULL;
    if(inherited){size_t n=strlen(inherited)+1;saved=malloc(n);assert(saved);memcpy(saved,inherited,n);}
    environment(NULL);
    guards_before_native_reads();fresh_metadata_and_clock();transitions_and_invalid_roots();
    optional_fields_remain_untrusted();actual_canonicalization_effect();
    environment(saved);free(saved);
    puts("render local tests passed: fresh primary/root, publisher identity fence, no stale fallback, unchanged clock; no head-additive fix claimed");
    return 0;
}

// Reuse the existing mock-memory builder, not native game memory or assets.
#define main render_local_fixture_main
#include "render_local_test.c"
#undef main
static void held(Fixture *f,uint64_t pointer,uint16_t clip,float phase,float length) {
    f->layout.off_carried_actor=352;
    put(f->actors[0],352,&pointer,8);
    put(f->actors[0],f->layout.off_anim_ids+10,&clip,2);
    size_t track=f->layout.off_anim_tracks+5*f->layout.anim_track_stride;
    put(f->actors[0],track+f->layout.off_anim_frame,&phase,4);
    put(f->actors[0],track+f->layout.off_anim_length,&length,4);
}
int main(void) {
    Fixture f;initialize(&f);SstLocal p=sst_game_read_local(&f.layout,7);
    assert(p.valid && !p.pose.equip_flags);
    held(&f,UINT64_C(1),431,4.5f,9); // Never dereference this deliberately invalid pointer.
    SstLocal v=sst_game_read_local(&f.layout,7);
    assert(v.valid && v.pose.equip_flags==SST_POSE_CONTEXT_SMALL_CARRY);
    SstLocal render=sst_game_read_render_local(&f.layout,&p,7);
    assert(render.valid && render.pose.equip_flags==SST_POSE_CONTEXT_SMALL_CARRY && render.pose.tick==7);
    animation(&f,2,20,7,24,3);v=sst_game_read_local(&f.layout,7);
    assert(v.pose.anim_id==20 && v.pose.action==3 && v.pose.equip_flags==SST_POSE_CONTEXT_SMALL_CARRY);
    held(&f,0,431,4.5f,9);assert(!sst_game_read_local(&f.layout,7).pose.equip_flags);
    held(&f,1,431,4.f,9);assert(sst_game_read_local(&f.layout,7).pose.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED);
    held(&f,1,430,4.5f,9);assert(sst_game_read_local(&f.layout,7).pose.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED);
    held(&f,1,431,NAN,9);assert(sst_game_read_local(&f.layout,7).pose.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED);
    held(&f,1,431,4.5f,8);assert(sst_game_read_local(&f.layout,7).pose.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED);
    held(&f,1,431,4.5f,9);f.layout.off_anim_length=0;
    assert(sst_game_read_local(&f.layout,7).pose.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED);
    f.layout.off_carried_actor=UINT32_MAX;
    assert(sst_game_read_local(&f.layout,7).pose.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED);
    f.layout.off_carried_actor=0;assert(!sst_game_read_local(&f.layout,7).pose.equip_flags);
    initialize(&f);held(&f,1,431,4.5f,9);f.layout.off_anim_tracks=UINT32_MAX-8;
    assert(sst_game_read_local(&f.layout,7).pose.equip_flags==SST_POSE_CONTEXT_UNSUPPORTED);
    puts("carry reader: native held/slot5 descriptor, invalid and inactive overlays, fresh render context and no carried-pointer dereference PASS");
    return 0;
}

// Reuse only the existing mock-memory fixture, never native addresses/assets.
#define main render_local_fixture_main
#include "render_local_test.c"
#undef main
#include "../src/sst_avatar.h"

#define MOCK_PLAYER_VTABLE UINT64_C(0x123456780000)
#define MOCK_ITEM_OFFSET 344u
static void configure_sword(Fixture *f,uint16_t item) {
    f->layout.off_item_being_used=MOCK_ITEM_OFFSET;
    f->layout.expected_player_vtable=MOCK_PLAYER_VTABLE;
    uint64_t vtable=MOCK_PLAYER_VTABLE;put(f->actors[0],0,&vtable,8);
    put(f->actors[0],MOCK_ITEM_OFFSET,&item,2);
}
static void expect_state(const Fixture *f,SstSwordUse state,uint8_t equipment) {
    uint32_t tick=sst_game_pose_tick();
    SstLocal local=sst_game_read_local(&f->layout,73);
    assert(local.valid && local.sword_use==state && local.pose.tick==73);
    assert((local.pose.equip_flags&3)==equipment);
    assert(sst_game_pose_tick()==tick);
}
static void native_predicate_without_hand_learning(void) {
    Fixture f;initialize(&f);sst_avatar_equipment_reset();
    assert(!sst_avatar_local_equipment());
    expect_state(&f,SST_SWORD_USE_UNKNOWN,0);
    const uint16_t active_clip[]={26,18,19,20,27,191,193,199,203,207};
    configure_sword(&f,17);
    for(unsigned i=0;i<sizeof active_clip/sizeof *active_clip;i++) {
        uint16_t action=active_clip[i]>=191?109:(active_clip[i]==20 || active_clip[i]==27?3:0);
        animation(&f,2,active_clip[i],9,45,action);
        expect_state(&f,SST_SWORD_USE_IN_USE,3);
        SstLocal local=sst_game_read_local(&f.layout,73);
        assert(sst_game_equipment_flags(&local,0)==3);
        assert(!sst_avatar_local_equipment()); // no draw-based observation was needed
    }
    const uint16_t other_item[]={11,0,1,7,16,18,UINT16_MAX};
    for(unsigned i=0;i<sizeof other_item/sizeof *other_item;i++) {
        put(f.actors[0],MOCK_ITEM_OFFSET,&other_item[i],2);
        expect_state(&f,SST_SWORD_USE_NOT_IN_USE,0);
        SstLocal local=sst_game_read_local(&f.layout,73);
        assert(sst_game_equipment_flags(&local,3)==SST_EQUIP_SWORD);
        assert(!sst_game_equipment_flags(&local,SST_EQUIP_SWORD_DRAWN));
    }
    assert(SST_LAYOUT_SSHD_100.off_item_being_used==0x6404);
    assert(SST_LAYOUT_SSHD_100.expected_player_vtable==UINT64_C(0x7001637d28));
}
static void identity_and_range_fences(void) {
    Fixture f;initialize(&f);configure_sword(&f,17);
    expect_state(&f,SST_SWORD_USE_IN_USE,3);
    uint64_t wrong=MOCK_PLAYER_VTABLE+8;put(f.actors[0],0,&wrong,8);
    expect_state(&f,SST_SWORD_USE_UNAVAILABLE,0);
    SstLocal local=sst_game_read_local(&f.layout,7);
    assert(sst_game_equipment_flags(&local,3)==SST_EQUIP_SWORD);
    // A deliberately unreachable optional field must NEVER be touched when the
    // vtable mismatches, even when nominal address bounds are not configured.
    f.layout.guest_lo=f.layout.guest_hi=0;f.layout.off_item_being_used=UINT32_MAX;
    expect_state(&f,SST_SWORD_USE_UNAVAILABLE,0);
    f.layout.expected_player_vtable=0;expect_state(&f,SST_SWORD_USE_UNAVAILABLE,0);
    initialize(&f);configure_sword(&f,17);
    // The existing pointer fence checks the actor base, so excluding its vtable
    // also excludes the root. Reject the complete actor before optional reads.
    f.layout.guest_lo=f.player+8;assert(!sst_game_read_local(&f.layout,73).valid);
    initialize(&f);configure_sword(&f,17);
    // Only one of the required two active-item bytes fits.
    f.layout.guest_hi=f.player+MOCK_ITEM_OFFSET+1;
    expect_state(&f,SST_SWORD_USE_UNAVAILABLE,0);
    initialize(&f);configure_sword(&f,17);f.layout.off_item_being_used=UINT32_MAX;
    expect_state(&f,SST_SWORD_USE_UNAVAILABLE,0);
    // A disabled optional field preserves legacy behavior regardless of vtable.
    f.layout.off_item_being_used=0;f.layout.expected_player_vtable=0;
    expect_state(&f,SST_SWORD_USE_UNKNOWN,0);
    local=sst_game_read_local(&f.layout,7);assert(sst_game_equipment_flags(&local,3)==3);
}
static void equipment_merge_contract(void) {
    SstLocal local={0};
    for(unsigned observed=0;observed<=UINT8_MAX;observed++) {
        local.sword_use=SST_SWORD_USE_UNKNOWN;
        assert(sst_game_equipment_flags(&local,(uint8_t)observed)==(observed&3));
        local.sword_use=SST_SWORD_USE_IN_USE;
        assert(sst_game_equipment_flags(&local,(uint8_t)observed)==3);
        local.sword_use=SST_SWORD_USE_NOT_IN_USE;
        assert(sst_game_equipment_flags(&local,(uint8_t)observed)==(observed&1));
        local.sword_use=SST_SWORD_USE_UNAVAILABLE;
        assert(sst_game_equipment_flags(&local,(uint8_t)observed)==(observed&1));
    }
    // The renderer must choose drawn-equipment identities from the SAME native
    // state as the publisher, including before any hand mesh was observed.
    for(uint16_t piece=55;piece<=57;piece++) {
        local.sword_use=SST_SWORD_USE_IN_USE;
        assert(sst_avatar_equipment_pose(piece,sst_game_equipment_flags(&local,0)&SST_EQUIP_SWORD_DRAWN)==piece+3);
        local.sword_use=SST_SWORD_USE_NOT_IN_USE;
        assert(sst_avatar_equipment_pose(piece,sst_game_equipment_flags(&local,3)&SST_EQUIP_SWORD_DRAWN)==piece);
        local.sword_use=SST_SWORD_USE_UNAVAILABLE;
        assert(sst_avatar_equipment_pose(piece,sst_game_equipment_flags(&local,3)&SST_EQUIP_SWORD_DRAWN)==piece);
    }
    local.sword_use=(SstSwordUse)99;
    assert(sst_game_equipment_flags(&local,0xff)==1 && sst_game_equipment_flags(NULL,0xff)==1);
}
static void fresh_context_and_publisher_immutability(void) {
    Fixture f;initialize(&f);configure_sword(&f,11);
    SstLocal published=sst_game_read_local(&f.layout,7),saved;
    assert(published.sword_use==SST_SWORD_USE_NOT_IN_USE);memcpy(&saved,&published,sizeof saved);
    // Independent carry context, with a non-dereferenceable carried actor value.
    f.layout.off_carried_actor=352;uint64_t carried=1;put(f.actors[0],352,&carried,8);
    uint16_t overlay=431;float phase=4.5f,length=9;
    put(f.actors[0],f.layout.off_anim_ids+10,&overlay,2);
    size_t slot=f.layout.off_anim_tracks+5*f.layout.anim_track_stride;
    put(f.actors[0],slot+f.layout.off_anim_frame,&phase,4);
    put(f.actors[0],slot+f.layout.off_anim_length,&length,4);
    uint16_t item=17;put(f.actors[0],MOCK_ITEM_OFFSET,&item,2);
    animation(&f,2,20,10,24,3);uint32_t clock=sst_game_pose_tick();
    SstLocal fresh=sst_game_read_render_local(&f.layout,&published,91);
    assert(fresh.valid && fresh.sword_use==SST_SWORD_USE_IN_USE && fresh.pose.tick==91);
    assert(fresh.pose.anim_id==20 && fresh.pose.anim_frame==10 && fresh.pose.action==3);
    assert(fresh.pose.equip_flags==(SST_POSE_CONTEXT_SMALL_CARRY|3));
    assert(!memcmp(&published,&saved,sizeof saved) && sst_game_pose_tick()==clock);
    // The publisher's low-bit replacement preserves context/world/motion bits.
    fresh.pose.equip_flags|=0xe0;
    uint8_t merged=(uint8_t)((fresh.pose.equip_flags&~3)|sst_game_equipment_flags(&fresh,0));
    assert(merged==(SST_POSE_CONTEXT_SMALL_CARRY|0xe0|3));
    item=11;put(f.actors[0],MOCK_ITEM_OFFSET,&item,2);
    fresh=sst_game_read_render_local(&f.layout,&published,91);
    assert(fresh.valid && fresh.sword_use==SST_SWORD_USE_NOT_IN_USE);
    assert(fresh.pose.equip_flags==SST_POSE_CONTEXT_SMALL_CARRY);
    assert(sst_game_equipment_flags(&fresh,3)==1);
    uint64_t wrong=MOCK_PLAYER_VTABLE+8;put(f.actors[0],0,&wrong,8);
    fresh=sst_game_read_render_local(&f.layout,&published,91);
    assert(fresh.valid && fresh.sword_use==SST_SWORD_USE_UNAVAILABLE);
    assert(fresh.pose.equip_flags==SST_POSE_CONTEXT_SMALL_CARRY && sst_game_equipment_flags(&fresh,3)==1);
    assert(!memcmp(&published,&saved,sizeof saved) && sst_game_pose_tick()==clock);
    SstGameLayout inaccessible=f.layout;inaccessible.player_ptr=1;
    environment("1");zero_local(sst_game_read_render_local(&inaccessible,&published,91));
    environment("0");zero_local(sst_game_read_render_local(&inaccessible,&published,91));
    environment(NULL);
}
int main(void) {
    const char *old=getenv("SST_NO_PLAYER_READ");char *saved=NULL;
    if(old){size_t size=strlen(old)+1;saved=malloc(size);assert(saved);memcpy(saved,old,size);}
    environment(NULL);
    native_predicate_without_hand_learning();identity_and_range_fences();
    equipment_merge_contract();fresh_context_and_publisher_immutability();
    environment(saved);free(saved);
    puts("sword state: bounded native predicate, cold flags, stale DRAWN rejection, unchanged tick/context PASS; no cold pose generation claimed");
    return 0;
}

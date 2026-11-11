// Exercise the actual bridge after a remote placement, without a game or GPU.
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main

static void expect_local_target(SstPropPhase phase) {
    SstProp target;
    assert(sst_props_get(&props_bridge.state, &prop_binding(actor)->key, now_ms, &target));
    assert(target.from == local_player_id && target.phase == phase);
}

static void local_pickup_after_place(const SstPropNativeDescriptor *d, uint16_t mine) {
    local_player_id = mine;
    setup(d); execute(); now_ms = 1101; execute();
    now_ms = 1102; send_remote(1, SST_PROP_CARRY); execute();
    now_ms = 1150; send_remote(2, SST_PROP_PLACE); execute();
    expect_position(500, 1.5f, 600); expect_unheld();
    assert(!sends && !prop_binding(actor)->rest.ready);

    // Native pickup supplies the complete held linkage; the bridge must retain
    // it and announce the new local owner even when its ID is higher.
    local.pose.pos[0] = 500; local.pose.pos[2] = 600;
    const uint64_t backlink = arena_lo + 0x7000;
    put64(player + 0x1b90, actor); w32(actor + 0x358, 1); w32(actor + 0x2e4, 1);
    put64(actor + 0x2d0, player); put64(actor + 0x2d8, backlink); put64(backlink + 0x10, actor);
    now_ms = 1200; actual_pose(500, 101.5f, 600);
    unsigned before = original_calls, models_before = model_calls;
    execute(); expect_position(500, 101.5f, 600);
    assert(original_calls == before + 1 && model_calls == models_before);
    assert(sends == 1 && captured.phase == SST_PROP_CARRY);
    expect_local_target(SST_PROP_CARRY);
    assert(!prop_binding(actor)->remote_held && !prop_binding(actor)->rest.ready);

    // Duplicate PLACE and an older CARRY from the previous owner cannot take
    // the actor back. Neither packet may disturb the real native held links.
    now_ms = 1201; send_remote(2, SST_PROP_PLACE); execute();
    now_ms = 1202; send_remote(1, SST_PROP_CARRY); execute();
    expect_position(500, 101.5f, 600); expect_local_target(SST_PROP_CARRY);
    assert(sends == 1 && model_calls == models_before);
    assert(r64(player + 0x1b90) == actor && r64(actor + 0x2d0) == player);
    assert(r64(actor + 0x2d8) == backlink && r64(backlink + 0x10) == actor);
    assert(r32(actor + 0x358) == 1 && r32(actor + 0x2e4) == 1);

    // A native putdown establishes a different floor position. Subsequent
    // remote carry expiry must restore this position, not the old remote one.
    put64(player + 0x1b90, 0); put64(actor + 0x2d0, 0); put64(actor + 0x2d8, 0);
    put64(backlink + 0x10, 0); w32(actor + 0x358, 0); w32(actor + 0x2e4, 0);
    now_ms = 1400; actual_pose(501, 1.5f, 601); execute();
    assert(sends == 2 && captured.phase == SST_PROP_PLACE);
    expect_local_target(SST_PROP_PLACE); expect_unheld();
    now_ms = 1501; execute();
    assert(prop_binding(actor)->rest.ready);
    now_ms = 1502; send_remote(3, SST_PROP_CARRY); execute();
    expect_position(1000, 101.5f, -1000); expect_unheld();
    now_ms = 3103; tick(); execute();
    expect_position(501, 1.5f, 601); expect_unheld();
    assert(!prop_binding(actor)->remote_held && sst_prop_presentation_stats.parked_presented == 1);
    printf("profile%u local%u: remote PLACE, local pickup/putdown, delayed packets and new-floor restoration PASS\n", d->profile, mine);
    finish();
}

static void departing_remote_owner(const SstPropNativeDescriptor *d, uint16_t mine) {
    local_player_id = mine;
    setup(d); execute(); now_ms = 1101; execute();
    now_ms = 1102; send_remote(1, SST_PROP_CARRY); execute();
    expect_position(1000, 101.5f, -1000);
    remote_connected = false; now_ms = 1200; tick(); execute();
    expect_position(10, 1.5f, 20); expect_unheld();
    assert(!prop_binding(actor)->remote_held && !sends);
    // A late descriptor cannot restore authority to the removed roster entry.
    now_ms = 1201; send_remote(2, SST_PROP_CARRY); execute();
    expect_position(10, 1.5f, 20); expect_unheld();
    assert(!prop_binding(actor)->remote_held && !sends);
    printf("profile%u local%u: remote owner leaves, native floor restored, retired packet denied PASS\n", d->profile, mine);
    finish();
}

int main(void) {
    for (unsigned i = 0; i < SST_PROP_NATIVE_ADAPTERS; i++) {
        local_pickup_after_place(&sst_prop_native_descriptors[i], 1);
        local_pickup_after_place(&sst_prop_native_descriptors[i], 3);
        departing_remote_owner(&sst_prop_native_descriptors[i], 1);
        departing_remote_owner(&sst_prop_native_descriptors[i], 3);
    }
    puts("Actual bridge ownership handoff CPU checks PASS; no game/GPU acceptance.");
    return 0;
}

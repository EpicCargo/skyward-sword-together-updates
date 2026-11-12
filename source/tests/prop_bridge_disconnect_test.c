// Actual bridge deactivation paths; synthetic native arena, no game/GPU.
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main

static void begin_remote_carry(const SstPropNativeDescriptor *d) {
    g_sst.sync_props = true;
    setup(d); execute(); now_ms = 1101; execute();
    now_ms = 1102; send_remote(1, SST_PROP_CARRY); execute();
    expect_position(1000, 101.5f, -1000);
    assert(prop_binding(actor)->remote_held && prop_binding(actor)->rest.ready);
    now_ms = 1200;
}

static void held_linkage(void) {
    uint64_t backlink = arena_lo + 0x7000;
    put64(player + 0x1b90, actor); w32(actor + 0x358, 1); w32(actor + 0x2e4, 1);
    put64(actor + 0x2d0, player); put64(actor + 0x2d8, backlink); put64(backlink + 0x10, actor);
    actual_pose(10, 101.5f, 20);
}

static void disconnect_tick(void) {
    unsigned char before[0x10000]; memcpy(before, arena, sizeof before);
    unsigned models = model_calls;
    network_online = false;
    prop_tick((SstLocal){0}, 0, false, false);
    assert(!memcmp(before, arena, sizeof before) && model_calls == models);
    assert(!props_bridge.enabled && !props_bridge.state.initialized);
}

static void check_mode(const SstPropNativeDescriptor *d, unsigned mode) {
    if (mode == 18) {
        g_sst.sync_props = true; setup(d);
        send_remote(1, SST_PROP_CARRY); execute();
        assert(!prop_binding(actor)->remote_held && !prop_binding(actor)->rest.ready);
        disconnect_tick(); execute(); expect_position(10, 1.5f, 20);
        assert(!sends); finish();
        printf("profile%u local%u deactivation mode18 PASS\n", d->profile, local_player_id);
        return;
    }
    begin_remote_carry(d);
    unsigned before = original_calls;
    if (mode >= 20 && mode <= 23) {
        // Pausing has already disabled ordinary packet presentation. A later
        // setting/connection change still owes this executing actor cleanup.
        prop_tick(local, 1, true, true);
        assert(!props_bridge.enabled && prop_binding(actor)->remote_held);
        if (mode == 21) network_online = false;
        else g_sst.sync_props = false;
        if (mode == 22) held_linkage();
        if (mode == 23) { local.player_instance = 2; actual_pose(35, 1.5f, 45); }
        execute();
        if (mode == 22) {
            expect_position(10, 101.5f, 20);
            assert(sst_prop_native_held(actor, player) && original_calls == before + 1);
        } else if (mode == 23) {
            expect_position(35, 1.5f, 45);
            assert(original_calls == before + 1);
        } else {
            expect_position(10, 1.5f, 20); expect_unheld();
            assert(original_calls == before);
            execute(); assert(original_calls == before + 1);
        }
        assert(!sends);
    } else if (mode == 1) {
        // This is the actual world bridge's offline call, without local data.
        disconnect_tick(); disconnect_tick();
        expect_position(1000, 101.5f, -1000); // No writes outside actor execute.
        execute(); expect_position(10, 1.5f, 20); expect_unheld();
        assert(original_calls == before && sends == 0);
        execute(); assert(original_calls == before + 1 && sends == 0);
    } else if (mode == 2 || mode == 3) {
        g_sst.sync_props = false;
        if (mode == 2) tick(); // mode3 disables between periodic input ticks.
        execute(); expect_position(10, 1.5f, 20); expect_unheld();
        assert(original_calls == before && !sends);
        execute(); assert(original_calls == before + 1 && !sends);
    } else if (mode == 4) {
        held_linkage(); g_sst.sync_props = false;
        // A setting can change before the next periodic prop_tick.
        execute(); expect_position(10, 101.5f, 20);
        assert(original_calls == before + 1 && sends == 0);
        assert(sst_prop_native_held(actor, player));
    } else if (mode == 5 || mode == 6 || mode == 7 || mode == 9 || mode == 16 || mode == 17) {
        disconnect_tick(); actual_pose(35, 1.5f, 45);
        if (mode == 5) w32(actor + 8, 43); // recycled actor at same address
        if (mode == 6) local.player_instance = 2;
        if (mode == 7) local.scene.room = 2;
        if (mode == 9) w32(actor + 0x358, 2); // active native simulation state
        if (mode == 16) local.valid = false;
        if (mode == 17) w32(actor + 0xc, 0xffffffe0); // placement identity changed
        execute(); expect_position(35, 1.5f, 45);
        assert(original_calls == before + 1 && !sends);
    } else if (mode == 8) {
        disconnect_tick(); held_linkage();
        execute(); expect_position(10, 101.5f, 20);
        assert(original_calls == before + 1 && !sends && sst_prop_native_held(actor, player));
        disconnect_tick(); execute(); expect_position(10, 101.5f, 20);
        assert(original_calls == before + 2 && !sends && sst_prop_native_held(actor, player));
    } else if (mode == 10 || mode == 11 || mode == 19) {
        if (mode == 10) disconnect_tick(); // reconnect before pending callback
        network_online = true; network_generation = 2; tick();
        expect_position(1000, 101.5f, -1000);
        execute(); expect_position(10, 1.5f, 20);
        assert(original_calls == before && !sends);
        if (mode == 19) {
            // A new session can reuse a peer ID, presence and sequence. Old
            // presentation stamps must not suppress its first valid PLACE.
            send_remote(1, SST_PROP_PLACE); execute(); expect_position(500, 1.5f, 600);
        } else {
            now_ms = 1400; execute(); now_ms = 1501; execute();
            assert(prop_binding(actor)->rest.ready);
            now_ms = 1502; send_remote(1, SST_PROP_CARRY); execute();
            expect_position(1000, 101.5f, -1000); assert(!sends);
        }
    } else if (mode == 12) {
        disconnect_tick(); actual_pose(35, 1.5f, 45);
        local.player_instance = 2; network_online = true;
        prop_tick(local, 2, true, false); execute(); expect_position(35, 1.5f, 45);
        assert(original_calls == before + 1 && !sends);
    } else if (mode == 13) {
        disconnect_tick(); put64(model, 0);
        execute(); expect_position(1000, 101.5f, -1000);
        assert(original_calls == before + 1 && !sends);
        put64(model, SST_PROP_MODEL_VTABLE); actual_pose(35, 1.5f, 45);
        disconnect_tick(); execute(); expect_position(35, 1.5f, 45);
        assert(original_calls == before + 2 && !sends);
    } else if (mode == 14) {
        send_remote(2, SST_PROP_PLACE); execute(); expect_position(500, 1.5f, 600);
        disconnect_tick(); execute(); expect_position(500, 1.5f, 600);
        assert(original_calls == before + 1 && !sends);
    } else if (mode == 15) {
        sst_prop_bridge_reset(); actual_pose(35, 1.5f, 45);
        execute(); expect_position(35, 1.5f, 45);
        assert(original_calls == before + 1 && !sends);
    } else assert(!"Unknown disconnect fixture mode");
    printf("profile%u local%u deactivation mode%u PASS\n", d->profile, local_player_id, mode);
    finish(); g_sst.sync_props = true;
}

int main(int argc, char **argv) {
    assert(argc == 2 || argc == 4);
    unsigned mode = (unsigned)strtoul(argv[1], NULL, 10);
    if (argc == 4) {
        const SstPropNativeDescriptor *d = sst_prop_native_descriptor((uint16_t)strtoul(argv[2], NULL, 10));
        local_player_id = (uint16_t)strtoul(argv[3], NULL, 10);
        assert(d && (local_player_id == 1 || local_player_id == 3));
        check_mode(d, mode); return 0;
    }
    for (unsigned i = 0; i < SST_PROP_NATIVE_ADAPTERS; i++) {
        local_player_id = 1; check_mode(&sst_prop_native_descriptors[i], mode);
        local_player_id = 3; check_mode(&sst_prop_native_descriptors[i], mode);
    }
    puts("Actual bridge deactivation CPU checks PASS; no game/GPU acceptance.");
    return 0;
}

// Actual actor callbacks resuming between paused synchronization ticks.
#define main presentation_fixture_main
#include "prop_bridge_presentation_test.c"
#undef main

static void begin_carry(const SstPropNativeDescriptor *d) {
    g_sst.sync_props = true; setup(d);
    execute(); now_ms = 1101; execute();
    now_ms = 1102; send_remote(1, SST_PROP_CARRY); execute();
    expect_position(1000, 101.5f, -1000);
    assert(prop_binding(actor)->remote_held && prop_binding(actor)->rest.ready);
    now_ms = 1200;
}

static void pause_tick(void) {
    unsigned char before[0x10000]; memcpy(before, arena, sizeof before);
    unsigned models = model_calls;
    prop_tick(local, 1, false, true); // Exact world-bridge event/menu pause call.
    assert(!memcmp(before, arena, sizeof before) && model_calls == models);
    assert(!props_bridge.enabled && props_bridge.state.initialized);
}

static void local_pickup(void) {
    uint64_t backlink = arena_lo + 0x7000;
    put64(player + 0x1b90, actor); w32(actor + 0x358, 1); w32(actor + 0x2e4, 1);
    put64(actor + 0x2d0, player); put64(actor + 0x2d8, backlink); put64(backlink + 0x10, actor);
    actual_pose(10, 101.5f, 20);
}

static void check_mode(const SstPropNativeDescriptor *d, unsigned mode) {
    begin_carry(d); unsigned before = original_calls;
    if (mode == 5) { send_remote(2, SST_PROP_PLACE); execute(); expect_position(500, 1.5f, 600); }
    SstProp prior;
    assert(sst_props_get(&props_bridge.state, &prop_binding(actor)->key, now_ms, &prior));
    pause_tick();
    if (mode == 2) local_pickup();
    if (mode == 3) { local.player_instance = 2; actual_pose(35, 1.5f, 45); }
    if (mode == 4) put64(model, 0);
    if (mode == 6) tick(); // Resume sync before the pending actor callback.
    // A pause must not reset the authenticated stream's replay evidence.
    assert(!sst_props_update(&props_bridge.state, prior.from, prior.presence_generation, &prior, now_ms));
    execute();
    if (mode == 1 || mode == 6 || mode == 7) {
        expect_position(10, 1.5f, 20); expect_unheld();
        assert(original_calls == before);
        if (mode == 1 || mode == 7) {
            actual_pose(35, 1.5f, 45);
            pause_tick(); execute(); expect_position(35, 1.5f, 45);
            assert(original_calls == before + 1); // No repeated old-floor write.
        }
        if (mode == 6 || mode == 7) {
            now_ms = 1400; tick(); execute();
            now_ms = 1501; execute();
            now_ms = 1502; send_remote(2, SST_PROP_CARRY); execute();
            expect_position(1000, 101.5f, -1000);
            if (mode == 7) {
                now_ms = 1503; pause_tick(); execute();
                expect_position(35, 1.5f, 45); // Newly observed native floor.
            }
        }
    } else if (mode == 2) {
        expect_position(10, 101.5f, 20);
        assert(sst_prop_native_held(actor, player) && original_calls == before + 1);
    } else if (mode == 3) {
        expect_position(35, 1.5f, 45); assert(original_calls == before + 1);
    } else if (mode == 4) {
        expect_position(1000, 101.5f, -1000); assert(original_calls == before + 1);
        put64(model, SST_PROP_MODEL_VTABLE); actual_pose(35, 1.5f, 45);
        pause_tick(); execute(); expect_position(35, 1.5f, 45);
        assert(original_calls == before + 2);
    } else if (mode == 5) {
        expect_position(500, 1.5f, 600); assert(original_calls == before + 1);
    } else assert(!"Unknown pause fixture mode");
    assert(!sends);
    printf("profile%u local%u pause mode%u PASS\n", d->profile, local_player_id, mode);
    finish();
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
    puts("Actual bridge pause CPU checks PASS; no game/GPU acceptance.");
    return 0;
}

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

static void check_mode(const SstPropNativeDescriptor *d, unsigned mode) {
    begin_remote_carry(d);
    unsigned before = original_calls;
    if (mode == 1) {
        network_online = false;
        // This is the actual world bridge's offline call, without local data.
        prop_tick((SstLocal){0}, 0, false, false);
        expect_position(1000, 101.5f, -1000); // No writes outside actor execute.
        execute(); expect_position(10, 1.5f, 20); expect_unheld();
        assert(original_calls == before && sends == 0);
        execute(); assert(original_calls == before + 1 && sends == 0);
    } else if (mode == 4) {
        held_linkage(); g_sst.sync_props = false;
        // A setting can change before the next periodic prop_tick.
        execute(); expect_position(10, 101.5f, 20);
        assert(original_calls == before + 1 && sends == 0);
        assert(sst_prop_native_held(actor, player));
    } else assert(!"Unknown disconnect fixture mode");
    printf("profile%u deactivation mode%u PASS\n", d->profile, mode);
    finish(); g_sst.sync_props = true;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    unsigned mode = (unsigned)strtoul(argv[1], NULL, 10);
    for (unsigned i = 0; i < SST_PROP_NATIVE_ADAPTERS; i++)
        check_mode(&sst_prop_native_descriptors[i], mode);
    puts("Actual bridge deactivation CPU checks PASS; no game/GPU acceptance.");
    return 0;
}

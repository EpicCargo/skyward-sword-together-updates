#include "../src/sst_props.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static SstProp vase(unsigned id) {
    SstProp p = {.scene = {.stage = "F001", .room = 2, .layer = 1, .entrance = 3},
        .key = {.placed_id = (uint16_t)id, .profile = SST_PROP_PROFILE,
                .params = 0x12340000, .room = 2, .layer = 1},
        .sequence = 1, .pose_tick = 37, .phase = SST_PROP_CARRY,
        .origin = {100, 200, 300}, .yaw = -12345,
        .matrix = {0, 0, 1, 120, 0, 1, 0, 250, -1, 0, 0, 320},
        .pos = {120, 250, 320}, .rot = {-32768, 32767, -1}};
    return p;
}
static void wire(void) {
    SstProp p = vase(42), decoded;
    uint8_t bytes[SST_PROP_WIRE_MAX + 1], second[SST_PROP_WIRE_MAX];
    memset(bytes, 0xaa, sizeof bytes);
    p.from = 65535; p.presence_generation = 123; p.received_ms = 999;
    size_t n = sst_prop_encode(&p, bytes, sizeof bytes);
    assert(n == 107 && bytes[n] == 0xaa);
    assert(bytes[0] == 4 && bytes[8] == 42 && bytes[10] == 0xf0 && bytes[11] == 1);
    assert(bytes[12] == 0 && bytes[13] == 0 && bytes[14] == 0x34 && bytes[15] == 0x12);
    assert(sst_prop_decode(bytes, n, &decoded));
    assert(!decoded.from && !decoded.presence_generation && !decoded.received_ms);
    assert(sst_prop_key_equal(&p.key, &decoded.key));
    assert(decoded.yaw == -12345 && decoded.rot[0] == -32768 && decoded.rot[1] == 32767 && decoded.rot[2] == -1);
    assert(sst_prop_encode(&decoded, second, sizeof second) == n && !memcmp(bytes, second, n));
    SstProp sentinel = vase(99), unchanged = sentinel;
    for (size_t i = 0; i < n; i++) {
        assert(!sst_prop_decode(bytes, i, &sentinel));
        assert(!memcmp(&sentinel, &unchanged, sizeof sentinel));
    }
    assert(!sst_prop_decode(bytes, n + 1, &sentinel));
    assert(!sst_prop_decode(NULL, n, &sentinel) && !sst_prop_decode(bytes, n, NULL));
    assert(!sst_prop_encode(&p, second, n - 1));
    uint8_t bad[SST_PROP_WIRE_MAX + 1];
    memcpy(bad, bytes, sizeof bad); bad[0] = 0; assert(!sst_prop_decode(bad, n, &sentinel));
    memcpy(bad, bytes, sizeof bad); bad[0] = 8; assert(!sst_prop_decode(bad, n, &sentinel));
    memcpy(bad, bytes, sizeof bad); bad[2] = 0; assert(!sst_prop_decode(bad, n, &sentinel));
    memcpy(bad, bytes, sizeof bad); bad[10] = 0xf2; assert(!sst_prop_decode(bad, n, &sentinel));
    memcpy(bad, bytes, sizeof bad); bad[22] = 3; assert(!sst_prop_decode(bad, n, &sentinel));
    memcpy(bad, bytes, sizeof bad); bad[41] = 0; bad[42] = 0; bad[43] = 0xc0; bad[44] = 0x7f;
    assert(!sst_prop_decode(bad, n, &sentinel)); // NaN matrix element
    strcpy(p.scene.stage, "F123456");
    assert(sst_prop_encode(&p, bytes, sizeof bytes) == SST_PROP_WIRE_MAX);
}
static void validation(void) {
    SstProp p = vase(4), q;
    assert(sst_prop_valid(&p));
    q = p; q.key.profile = 0x1f2; assert(!sst_prop_valid(&q));
    q = p; q.key.placed_id = 65535; assert(!sst_prop_valid(&q));
    q = p; q.key.params++; assert(sst_prop_valid(&q) && !sst_prop_key_equal(&p.key, &q.key));
    q = p; q.key.room++; assert(!sst_prop_valid(&q));
    q = p; q.key.room = 255; assert(sst_prop_valid(&q));
    q = p; q.key.layer++; assert(!sst_prop_valid(&q));
    q = p; strcpy(q.scene.stage, "../F001"); assert(!sst_prop_valid(&q));
    q = p; memset(q.scene.stage, 'F', SST_STR); assert(!sst_prop_valid(&q));
    q = p; q.phase = 0; assert(!sst_prop_valid(&q));
    for (unsigned i = 0; i < 12; i++) {
        q = p; q.matrix[i] = NAN; assert(!sst_prop_valid(&q));
        q.matrix[i] = INFINITY; assert(!sst_prop_valid(&q));
    }
    for (unsigned i = 0; i < 3; i++) {
        q = p; q.pos[i] = NAN; assert(!sst_prop_valid(&q));
        q = p; q.origin[i] = 1000001; assert(!sst_prop_valid(&q));
    }
    q = p; q.matrix[2] = 2; assert(!sst_prop_valid(&q)); // scale
    q = p; q.matrix[8] = 1; assert(!sst_prop_valid(&q)); // reflection
    q = p; q.matrix[4] = 0; q.matrix[5] = 0; q.matrix[6] = 1;
    assert(!sst_prop_valid(&q)); // individually unit but non-orthogonal rows
    q = p; q.matrix[3] += 129; assert(!sst_prop_valid(&q));
    q = p; q.origin[0] -= 1000; assert(!sst_prop_valid(&q));
    q.phase = SST_PROP_PLACE; assert(sst_prop_valid(&q)); // placer can subsequently walk away
}
static void reviewed_profiles(void) {
    static const uint16_t profiles[] = {SST_PROP_PROFILE, SST_PROP_BIG_PROFILE, SST_PROP_BARREL_PROFILE};
    SstProp p = vase(17), out; uint8_t bytes[SST_PROP_WIRE_MAX];
    SstPropState state = {0}; sst_props_begin(&state, &p.scene, 1);
    // Same placement/params under different native classes are distinct identities.
    p.key.params = 0xfffffff0;
    for (unsigned i = 0; i < sizeof profiles / sizeof *profiles; i++) {
        p.key.profile = profiles[i];
        assert(sst_prop_profile_allowed(profiles[i], p.key.params));
        assert(sst_prop_valid(&p));
        size_t n = sst_prop_encode(&p, bytes, sizeof bytes);
        assert(n == 107 && sst_prop_decode(bytes, n, &out));
        assert(out.key.profile == profiles[i] && out.key.params == p.key.params);
        assert(sst_props_update(&state, (uint16_t)(i + 1), 1, &p, 1000));
    }
    for (unsigned i = 0; i < sizeof profiles / sizeof *profiles; i++) {
        p.key.profile = profiles[i];
        assert(sst_props_get(&state, &p.key, 1000, &out));
        assert(out.from == i + 1 && out.key.profile == profiles[i]);
    }
    // Exhaust all subtype and NPC-mode bits; even well-formed wire must reject
    // bomb/Type2/Kraken barrels and normal barrels used by NPC quest modes.
    for (unsigned bits = 0; bits < 256; bits++) {
        uint32_t params = 0xffffff00u | bits;
        bool expected = ((bits & 15) == 0 || (bits & 15) == 15) && ((bits >> 6) & 3) >= 2;
        assert(sst_prop_profile_allowed(SST_PROP_BARREL_PROFILE, params) == expected);
        p.key.profile = SST_PROP_BARREL_PROFILE; p.key.params = params;
        assert(sst_prop_valid(&p) == expected);
        size_t n = sst_prop_encode(&p, bytes, sizeof bytes);
        assert((n != 0) == expected);
        if (expected) assert(sst_prop_decode(bytes, n, &out));
    }
    static const uint16_t unsupported[] = {0, 0x14, 0x149, 0x14a, 0x1a9, 0x1aa, 0x1ab, 0x1b8, 0xffff};
    p.key.params = 0xfffffff0;
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; i++) {
        p.key.profile = unsupported[i];
        assert(!sst_prop_profile_allowed(p.key.profile, p.key.params));
        assert(!sst_prop_valid(&p) && !sst_prop_encode(&p, bytes, sizeof bytes));
    }
    p.key.profile = SST_PROP_BIG_PROFILE; p.key.placed_id = 0xffff;
    assert(!sst_prop_valid(&p)); // Dynamic child identity has not been introduced.
}
static void ownership(void) {
    SstProp p = vase(4), out; SstPropState a = {0}, b = {0};
    sst_props_begin(&a, &p.scene, 10); sst_props_begin(&b, &p.scene, 10);
    assert(sst_props_update(&a, 2, 7, &p, 1000));
    assert(sst_props_update(&a, 1, 7, &p, 1000));
    assert(sst_props_update(&b, 1, 7, &p, 1000));
    assert(!sst_props_update(&b, 2, 7, &p, 1000));
    assert(sst_props_get(&a, &p.key, 2500, &out) && out.from == 1);
    assert(sst_props_get(&b, &p.key, 2500, &out) && out.from == 1);
    assert(!sst_props_get(&a, &p.key, 2501, &out));
    assert(!sst_props_get(&a, &p.key, 999, &out)); // clock cannot precede arrival
    assert(!sst_props_update(&a, 2, 7, &p, 2501)); // rejected contender's old sequence remembered
    p.sequence = 2;
    assert(sst_props_update(&a, 2, 7, &p, 2501)); // refreshed contender takes expired carry
    p.sequence = 3; p.phase = SST_PROP_PLACE;
    assert(sst_props_update(&a, 2, 7, &p, 2600));
    assert(sst_props_get(&a, &p.key, 999999, &out) && out.phase == SST_PROP_PLACE);
    p.sequence = 2;
    assert(!sst_props_update(&a, 1, 7, &p, 2601)); // another peer cannot place current owner's vase
    p.sequence = 3; p.phase = SST_PROP_CARRY;
    assert(sst_props_update(&a, 1, 7, &p, 2700)); // new pickup after placement
    p.sequence = 4; p.phase = SST_PROP_PLACE;
    assert(!sst_props_update(&a, 2, 7, &p, 2701)); // previous owner's delayed place cannot steal carry
    p.phase = SST_PROP_CARRY; p.sequence = 4;
    assert(sst_props_update(&a, 1, 7, &p, 2800));
    assert(!sst_props_update(&a, 1, 7, &p, 2800));
    p.sequence = 2; assert(!sst_props_update(&a, 1, 7, &p, 2900));
    assert(sst_props_get(&a, &p.key, 2900, &out) && out.sequence == 4);
    SstPropKey other = p.key; other.params++;
    assert(!sst_props_get(&a, &other, 2900, &out));
    other = p.key; other.placed_id++;
    assert(!sst_props_get(&a, &other, 2900, &out));
}
static void lifecycle(void) {
    SstProp p = vase(7), out; SstPropState s = {0};
    assert(!sst_props_update(&s, 1, 7, &p, 1000));
    sst_props_begin(&s, &p.scene, 10);
    assert(!sst_props_update(&s, 0, 7, &p, 1000));
    SstProp wrong = p; wrong.scene.layer++;
    assert(!sst_props_update(&s, 1, 7, &wrong, 1000));
    p.received_ms = 1000;
    assert(!sst_props_update(&s, 1, 7, &p, 2501));
    assert(!sst_props_update(&s, 1, 7, &p, 999));
    assert(sst_props_update(&s, 1, 7, &p, 2000));
    assert(sst_props_get(&s, &p.key, 2500, &out));
    assert(!sst_props_get(&s, &p.key, 2501, &out)); // draining a queue cannot renew its arrival
    sst_props_reset(&s); sst_props_begin(&s, &p.scene, 10);
    p.received_ms = 0; p.sequence = UINT32_MAX;
    assert(sst_props_update(&s, 1, 7, &p, 1000));
    p.sequence = 0; assert(sst_props_update(&s, 1, 7, &p, 1001)); // sequence wrap
    p.sequence = UINT32_MAX; assert(!sst_props_update(&s, 1, 7, &p, 1002));
    sst_props_forget(&s, 1, 6); assert(sst_props_get(&s, &p.key, 1003, &out));
    sst_props_forget(&s, 1, 7); assert(!sst_props_get(&s, &p.key, 1003, &out));
    p.sequence = 1; assert(!sst_props_update(&s, 1, 7, &p, 1004));
    assert(sst_props_update(&s, 1, 8, &p, 1004));
    p.sequence = 100; assert(!sst_props_update(&s, 1, 7, &p, 1005));
    sst_props_forget(&s, 1, 7); assert(sst_props_get(&s, &p.key, 1005, &out));
    assert(out.presence_generation == 8 && out.received_ms == 1004 && out.from == 1);
    SstScene area = p.scene; area.entrance++;
    sst_props_begin(&s, &area, 10); assert(sst_props_get(&s, &p.key, 1005, &out));
    sst_props_begin(&s, &area, 11); assert(!sst_props_get(&s, &p.key, 1005, &out));
    p.phase = SST_PROP_PLACE; assert(sst_props_update(&s, 1, 8, &p, 1006)); // initial placed announcement
    area.room++; sst_props_begin(&s, &area, 11);
    assert(!sst_props_get(&s, &p.key, 1006, &out));
    assert(!sst_props_update(&s, 1, 8, &p, 1006));
    sst_props_begin(&s, NULL, 0); assert(!s.initialized);
    sst_props_reset(&s); assert(!s.initialized);
}
static void bounded_cache(void) {
    SstProp p = vase(0), out; SstPropState s = {0}; sst_props_begin(&s, &p.scene, 1);
    p.phase = SST_PROP_PLACE;
    for (unsigned i = 0; i < SST_PROPS; i++) {
        p.key.placed_id = (uint16_t)i;
        assert(sst_props_update(&s, 1, 1, &p, 1000));
    }
    p.key.placed_id = SST_PROPS;
    assert(!sst_props_update(&s, 1, 1, &p, 1000));
    for (unsigned i = 0; i < SST_PROPS; i++) {
        p.key.placed_id = (uint16_t)i;
        assert(sst_props_get(&s, &p.key, UINT64_MAX, &out));
    }
    sst_props_reset(&s); sst_props_begin(&s, &p.scene, 1);
    p.phase = SST_PROP_CARRY;
    for (unsigned i = 1; i <= SST_MAX_PLAYERS; i++) {
        p.key.placed_id = (uint16_t)i;
        assert(sst_props_update(&s, (uint16_t)i, 1, &p, 1000));
    }
    p.key.placed_id++;
    assert(!sst_props_update(&s, SST_MAX_PLAYERS + 1, 1, &p, 1000));
}
int main(void) {
    wire(); validation(); reviewed_profiles(); ownership(); lifecycle(); bounded_cache();
    puts("props: strict wire/matrix validation, reviewed profiles/subtypes, exact identity, ownership, expiry, placement, generations and bounds PASS");
    return 0;
}

// SPDX-License-Identifier: MIT
// Adapted from the user-provided SST handoff dated2026-10-30.
// Mesh census (SST_MESH_CENSUS): bounded logging, wrist classification as observation only. No game, no network.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) assert(c)
#define RUN(fn) do { fn(); } while (0)
#define TEST_EXIT() do { puts("census: bounded observations, parent/wrist evidence and no authorization PASS"); return 0; } while (0)
#include "../src/sst_animation.h"
#include "../src/sst_avatar.h"
#include "../src/sst_census.h"
#include <math.h>

#define BODY 6
#define HAND 23
#define PALETTE (48 * 8)
static const float view[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
static const float identity[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
static float body_palette[PALETTE / 4], normals[PALETTE / 4];
static const SstScene scene = {"F000", 1, 0, 0};
static uint64_t g_frame;
static FILE *out;

static SstPose pose(uint16_t clip, uint16_t action, float frame) {
    return (SstPose){.tick = 1, .anim_id = clip, .anim_frame = frame, .action = action, .health = 12, .max_health = 12};
}
static void at_x(float m[12], float x) { memcpy(m, identity, 48); m[3] = x; }
// Teach the animation library a right-hand wrist link: hand on body joint 5 in clips 26 and 20.
static void learn_wrist(uint16_t clip, float length) {
    float hand[12];
    at_x(hand, 50);
    sst_animation_set_length(clip, length);
    static uint32_t tick = 1;
    for (float t = 0; t <= length + 9; t += 1) {
        SstPose l = pose(clip, 0, fmodf(t, length));
        l.tick = ++tick;
        sst_animation_frame(&scene, &l, ++g_frame);
        sst_animation_set_length(clip, length);
        sst_animation_capture(BODY, &l, view, identity, body_palette, PALETTE, normals, PALETTE);
        sst_animation_capture(HAND, &l, view, hand, NULL, 0, NULL, 0);
    }
}
static void setup(bool link) {
    for (unsigned j = 0; j < PALETTE / 48; j++) at_x(body_palette + j * 12, j * 10.0f), at_x(normals + j * 12, 0);
    sst_animation_reset();
    if (link) { learn_wrist(26, 60); learn_wrist(20, 40); }
    if (out) fclose(out);
    out = fopen("census_test.log", "w+");
    sst_census_reset(out);
}
static unsigned lines_with(const char *needle) {
    fflush(out);
    rewind(out);
    char line[600];
    unsigned n = 0;
    while (fgets(line, sizeof line, out)) if (strstr(line, needle)) n++;
    fseek(out, 0, SEEK_END);
    return n;
}
static void census(uint64_t hash, uint32_t size, uint16_t known, float x, const void *palette, uint64_t present) {
    SstCensusDraw d = {.hash = hash, .size = size, .positions = palette ? PALETTE : 0, .count = 300, .known = known};
    float m[12];
    at_x(m, x);
    SstPose local = pose(26, 0, 4);
    sst_census_draw(&d, &local, view, m, palette, present);
}

static void entries_logged_once(void) {
    setup(false);
    for (uint64_t p = 1; p <= 3; p++) { census(0xabc, 28672, 0, 40, NULL, p); census(0xabc, 28672, 0, 40, NULL, p); }
    census(0xdef, 4096, 0, 40, NULL, 4);
    CHECK(lines_with("[sst-mesh] new u1 ") == 1 && lines_with("[sst-mesh] new u2 ") == 1);
    SstCensusStats s = sst_census_stats();
    CHECK(s.entries == 2 && s.unknown_entries == 2 && s.unknown_rigid_near == 2);
    CHECK(s.unknown_hand_presents == 3); // presents 1-3 had the hand-sized unknown (present 4 not finished yet)
}
static void far_geometry_ignored(void) {
    setup(false);
    census(0xabc, 28672, 0, 5000, NULL, 1);
    census(0xabc, 28672, 0, 5000, NULL, 2);
    CHECK(sst_census_stats().entries == 0 && lines_with("new") == 0);
}
static void unknown_on_wrist_is_observed(void) {
    setup(true);
    CHECK(sst_animation_wrist_link(HAND, NULL, NULL, NULL));
    census(1, 9000, BODY, 0, body_palette, 10);       // donor body this present
    census(0x1111, 28672, 0, 50, NULL, 10);           // unknown on joint 5: right wrist
    census(0x2222, 2048, 0, 200, NULL, 10);           // unknown elsewhere: not a wrist
    census(1, 9000, BODY, 0, body_palette, 11);       // next present ends present 10
    CHECK(lines_with("wrist u1 side=R") == 1 && lines_with("wrist u2 side=-") == 1);
    SstCensusStats s = sst_census_stats();
    CHECK(s.unknown_on_wrist == 1 && s.unknown_rigid_near == 2);
    CHECK(sst_avatar_mesh_id(NULL, 0) == 0 && sst_animation_wrist_link(HAND, NULL, NULL, NULL)); // nothing mapped or changed
}
static void conflicting_observation_stays_unclassified(void) {
    setup(true);
    census(1, 9000, BODY, 0, body_palette, 10);
    census(0x1111, 28672, 0, 50, NULL, 10);
    census(1, 9000, BODY, 0, body_palette, 11);
    census(0x1111, 28672, 0, 70, NULL, 11);           // same mesh now on joint 7
    census(1, 9000, BODY, 0, body_palette, 12);
    census(0x1111, 28672, 0, 50, NULL, 12);           // and back on the wrist
    census(1, 9000, BODY, 0, body_palette, 13);
    CHECK(lines_with("wrist u1 side=R") == 1 && lines_with("wrist u1 side=?") == 1);
    CHECK(lines_with("wrist u1") == 2 && sst_census_stats().unknown_on_wrist == 0);
}
static void set_changes_are_rate_limited(void) {
    setup(false);
    for (uint64_t p = 1; p <= 240; p++) {
        census(0xaaa, 1000, 0, 10, NULL, p);
        if (p % 2) census(0xbbb, 1000, 0, 20, NULL, p);
    }
    unsigned sets = lines_with("[sst-mesh] set ");
    CHECK(sets >= 30 && sets <= 50);                  // at most 10 per 60 presents over 4 windows
    CHECK(sst_census_stats().suppressed > 100);
    CHECK(lines_with(" +u2") > 0 && lines_with(" -u2") > 0);
}
static void attached_piece_reports_its_joint(void) {
    setup(false);                                      // no wrist link needed: any recognised skinned body
    for (uint64_t p = 1; p <= 4; p++) {
        census(1, 9000, BODY, 0, body_palette, p);
        census(0x3333, 28672, 0, 30, NULL, p);          // on joint 3
        census(0x4444, 28672, 13, 60, NULL, p);         // a recognised hand-sized piece is cross-referenced too
    }
    census(1, 9000, BODY, 0, body_palette, 5);
    CHECK(lines_with("joint u1 body=k6 joint=3") == 1 && lines_with("joint k13 body=k6 joint=6") == 1);
    CHECK(lines_with("wrist k13") == 0);               // wrist tests stay for unrecognised pieces
    census(0x3333, 28672, 0, 40, NULL, 5);              // now on joint 4
    census(1, 9000, BODY, 0, body_palette, 6);
    census(0x3333, 28672, 0, 30, NULL, 6);
    census(1, 9000, BODY, 0, body_palette, 7);
    CHECK(lines_with("joint u1 conflict: joint 3 then k6 joint 4") == 1 && lines_with("joint u1") == 2);
}
static void child_piece_reports_parent_joint(void) {
    setup(false);
    float animated[PALETTE / 4];
    for (uint64_t p = 1; p <= 140; p++) {
        for (unsigned j = 0; j < PALETTE / 48; j++) {   // each joint turns at its own rate
            float a = p * .03f * (j + 1), *m = animated + j * 12;
            float r[12] = {cosf(a), -sinf(a), 0, j * 10.0f, sinf(a), cosf(a), 0, 5, 0, 0, 1, 0};
            memcpy(m, r, 48);
        }
        const float *parent = animated + 3 * 12;         // the hand: joint 3 times a fixed offset (8, 2, 1)
        float hand[12] = {parent[0], parent[1], parent[2], parent[0] * 8 + parent[1] * 2 + parent[2] + parent[3],
                          parent[4], parent[5], parent[6], parent[4] * 8 + parent[5] * 2 + parent[6] + parent[7],
                          parent[8], parent[9], parent[10], parent[8] * 8 + parent[9] * 2 + parent[10] + parent[11]};
        SstCensusDraw body = {.hash = 1, .size = 9000, .positions = PALETTE, .count = 300, .known = BODY};
        SstCensusDraw piece = {.hash = 0x7777, .size = 28672, .count = 744};
        SstPose l = pose(26, 0, 4);
        sst_census_draw(&body, &l, view, identity, animated, p);
        sst_census_draw(&piece, &l, view, hand, NULL, p);
    }
    CHECK(lines_with("parent u1 after") == 1);
    CHECK(lines_with("parent u1 after 120 presents: k6 joint 3 (agree 120, offset 8.0 2.0 1.0)") == 1);
}
// Session 007: an unrecognised non-hand piece (sword-sized) is searched too; when it moves to another parent
// (sheathed on the back, then drawn into the hand) the search restarts and reports the new joint and offset.
static void moved_piece_reports_each_parent(void) {
    setup(false);
    float animated[PALETTE / 4];
    for (uint64_t p = 1; p <= 260; p++) {
        for (unsigned j = 0; j < PALETTE / 48; j++) {
            float a = p * .03f * (j + 1), *m = animated + j * 12;
            float r[12] = {cosf(a), -sinf(a), 0, j * 10.0f, sinf(a), cosf(a), 0, 5, 0, 0, 1, 0};
            memcpy(m, r, 48);
        }
        bool drawn = p > 130;
        const float *parent = animated + (drawn ? 5 : 3) * 12;
        float o[3] = {drawn ? 0 : 8, drawn ? 4 : 2, drawn ? 0 : 1}, piece_model[12];
        for (int r = 0; r < 3; r++) {
            for (int c = 0; c < 3; c++) piece_model[r * 4 + c] = parent[r * 4 + c];
            piece_model[r * 4 + 3] = parent[r * 4] * o[0] + parent[r * 4 + 1] * o[1] + parent[r * 4 + 2] * o[2] + parent[r * 4 + 3];
        }
        SstCensusDraw body = {.hash = 1, .size = 9000, .positions = PALETTE, .count = 300, .known = BODY};
        SstCensusDraw piece = {.hash = 0x8888, .size = 32768, .count = 882};
        SstPose l = pose(26, 0, 4);
        sst_census_draw(&body, &l, view, identity, animated, p);
        sst_census_draw(&piece, &l, view, piece_model, NULL, p);
    }
    CHECK(lines_with("parent u1 after 120 presents: k6 joint 3 (agree 120, offset 8.0 2.0 1.0)") == 1);
    CHECK(lines_with("parent u1 after 120 presents: k6 joint 5 (agree 120, offset 0.0 4.0 0.0)") == 1);
    CHECK(lines_with("nearest k6 joint 3: max rotation dev 0.0000, max translation dev 0.00, offset 8.00 2.00 1.00") == 1);
    CHECK(lines_with("parent u1 after") == 2);
    CHECK(sst_avatar_mesh_id(NULL, 0) == 0); // observation only: nothing mapped
}
// Session 007: a transient hand-sized piece (one present per sighting) logs its offsets to hand joints 3 and 5,
// bounded per entry, so it can be compared with the recognised hand it replaces.
static void transient_hand_logs_joint_offsets(void) {
    setup(false);
    static float big[368 * 12];
    for (unsigned j = 0; j < 368; j++) at_x(big + j * 12, j * 10.0f);
    for (uint64_t p = 1; p <= 30; p++) {
        SstCensusDraw body = {.hash = 1, .size = 110592, .positions = 368 * 48, .count = 2664, .known = BODY};
        SstCensusDraw piece = {.hash = 0x9999, .size = 28672, .count = 744};
        SstCensusDraw known = {.hash = 0xaaaa, .size = 28672, .count = 744, .known = 52};
        SstPose l = pose(168, 2, 128);
        float m[12];
        at_x(m, 38);                                    // joint 3 (x = 30) + (8, 0, 0)
        m[7] = 2;
        sst_census_draw(&body, &l, view, identity, big, p);
        sst_census_draw(&known, &l, view, m, NULL, p);
        if (p % 3 == 0) sst_census_draw(&piece, &l, view, m, NULL, p); // a sighting every third present
    }
    CHECK(lines_with("offset u1 k6 joint3 8.00 2.00 0.00 joint5 -12.00 2.00 0.00 clip=168 act=2") == 8);
    CHECK(lines_with("offset k52 k6 joint3 8.00 2.00 0.00") == 2);
    CHECK(lines_with("[sst-mesh] offset") == 10);
}
static void bounded_entries(void) {
    setup(false);
    for (uint64_t i = 0; i < SST_CENSUS_ENTRIES + 40; i++) census(0x5000 + i, 1000, 0, 10, NULL, 1);
    SstCensusStats s = sst_census_stats();
    CHECK(s.entries == SST_CENSUS_ENTRIES && s.suppressed == 40 && lines_with("new") == SST_CENSUS_ENTRIES);
}

int main(void) {
    RUN(entries_logged_once);
    RUN(far_geometry_ignored);
    RUN(unknown_on_wrist_is_observed);
    RUN(conflicting_observation_stays_unclassified);
    RUN(set_changes_are_rate_limited);
    RUN(attached_piece_reports_its_joint);
    RUN(child_piece_reports_parent_joint);
    RUN(moved_piece_reports_each_parent);
    RUN(transient_hand_logs_joint_offsets);
    RUN(bounded_entries);
    if (out) fclose(out);
    remove("census_test.log");
    TEST_EXIT();
}

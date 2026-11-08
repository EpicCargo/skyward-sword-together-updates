// Unit tests for the game-agnostic modules, using mock game data.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/sst_backup.h"
#include "../src/sst_flags.h"
#include "../src/sst_game.h"
#include "../src/sst_pose_buffer.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static SstPose pose_at(uint32_t tick, float x) {
    SstPose p = {.tick = tick, .pos = {x, 0, 0}, .rot = {0, (int16_t)(tick * 100), 0}, .anim_id = 1, .anim_frame = (float)tick};
    return p;
}

static SstPose between(SstPose a, SstPose c, float phase, float length, bool loop) {
    SstPoseBuffer b;
    sst_pose_buffer_reset(&b);
    a.tick=10; c.tick=11;
    sst_pose_buffer_push(&b,&a,0);
    sst_pose_buffer_push(&b,&c,SST_TICK_MS);
    sst_pose_buffer_animation(&b,a.anim_id,length,loop);
    b.started=true; b.clock_tick=10+phase; b.last_update_ms=100;
    SstPose out;
    CHECK(sst_pose_buffer_sample(&b,100,&out));
    return out;
}

static void test_pose_buffer(void) {
    puts("pose buffer");
    SstPoseBuffer b;
    sst_pose_buffer_reset(&b);
    SstPose out;
    CHECK(!sst_pose_buffer_sample(&b, 0, &out));

    // steady 30 Hz stream: playback runs behind the newest sample and stays smooth
    double t = 0;
    for (uint32_t k = 0; k < 60; k++, t += SST_TICK_MS) {
        SstPose p = pose_at(k, k * 10.0f);
        sst_pose_buffer_push(&b, &p, t);
    }
    CHECK(sst_pose_buffer_sample(&b, t, &out));
    CHECK(out.pos[0] < 59 * 10.0f && out.pos[0] > 50 * 10.0f);  // 2..8 ticks behind
    float prev = out.pos[0];
    int monotonic = 1;
    for (uint32_t k = 60; k < 120; k++) {
        t += SST_TICK_MS / 2;
        SstPose p = pose_at(k, k * 10.0f);
        if (k % 2 == 0) sst_pose_buffer_push(&b, &p, t);  // every other half-frame: 30 Hz
        else { SstPose q = pose_at(k, k * 10.0f); sst_pose_buffer_push(&b, &q, t); }
        sst_pose_buffer_sample(&b, t, &out);
        if (out.pos[0] < prev - 0.001f) monotonic = 0;
        prev = out.pos[0];
    }
    CHECK(monotonic);

    // interpolated position between samples is between them, not snapped
    sst_pose_buffer_reset(&b);
    t = 0;
    for (uint32_t k = 0; k < 10; k++, t += SST_TICK_MS) { SstPose p = pose_at(k, k * 10.0f); sst_pose_buffer_push(&b, &p, t); }
    sst_pose_buffer_sample(&b, t, &out);
    sst_pose_buffer_sample(&b, t + SST_TICK_MS * 0.5, &out);
    CHECK(fmodf(out.pos[0], 10.0f) > 0.5f && fmodf(out.pos[0], 10.0f) < 9.5f);

    // late (out-of-order) packet is inserted, duplicates ignored
    sst_pose_buffer_reset(&b);
    SstPose p0 = pose_at(0, 0), p2 = pose_at(2, 20), p1 = pose_at(1, 10);
    sst_pose_buffer_push(&b, &p0, 0);
    sst_pose_buffer_push(&b, &p2, 66);
    sst_pose_buffer_push(&b, &p1, 70);
    sst_pose_buffer_push(&b, &p1, 71);
    CHECK(b.count == 3);
    CHECK(b.ring[b.head].tick == 2);
    CHECK(b.ring[(b.head + SST_POSE_RING - 1) % SST_POSE_RING].tick == 1);
    CHECK(b.ring[(b.head + SST_POSE_RING - 2) % SST_POSE_RING].tick == 0);

    // a big jump (teleport, respawn) snaps instead of sliding through walls
    sst_pose_buffer_reset(&b);
    t = 0;
    for (uint32_t k = 0; k < 10; k++, t += SST_TICK_MS) {
        SstPose p = pose_at(k, k < 5 ? 0.0f : 5000.0f);
        sst_pose_buffer_push(&b, &p, t);
    }
    int slid = 0;
    for (int i = 0; i < 40; i++) {
        sst_pose_buffer_sample(&b, t + i * 5.0, &out);
        if (out.pos[0] > 1.0f && out.pos[0] < 4999.0f) slid = 1;
    }
    CHECK(!slid);

    // rotation interpolates the short way across the -32768/32767 wrap
    sst_pose_buffer_reset(&b);
    SstPose r0 = pose_at(0, 0), r1 = pose_at(1, 0);
    r0.rot[1] = 32000; r1.rot[1] = -32000;  // 1536 units apart going through +-32768
    for (int k = 0; k < 4; k++) { r0.tick = (uint32_t)(k * 2); r1.tick = (uint32_t)(k * 2 + 1);
        sst_pose_buffer_push(&b, &r0, k * 2 * SST_TICK_MS); sst_pose_buffer_push(&b, &r1, (k * 2 + 1) * SST_TICK_MS); }
    int wrong_way = 0;
    for (int i = 0; i < 20; i++) {
        sst_pose_buffer_sample(&b, 8 * SST_TICK_MS + i * 3.0, &out);
        if (out.rot[1] > -31000 && out.rot[1] < 31000) wrong_way = 1;
    }
    CHECK(!wrong_way);

    // Known native loops interpolate forward through the end instead of snapping at each wrap.
    r0.anim_id=r1.anim_id=26; r0.anim_frame=44; r1.anim_frame=1;
    out=between(r0,r1,.25f,45,true); CHECK(fabsf(out.anim_frame-44.5f)<.001f);
    out=between(r0,r1,.5f,45,true); CHECK(fabsf(out.anim_frame)<.001f);
    out=between(r0,r1,.75f,45,true); CHECK(fabsf(out.anim_frame-.5f)<.001f);
    out=between(r0,r1,.25f,0,true); CHECK(out.anim_frame==44); // Unobserved duration: hold.
    r0.anim_frame=20; r1.anim_frame=1;
    out=between(r0,r1,.25f,45,true); CHECK(out.anim_frame==20); // A restart is not a loop wrap.
    r0.anim_id=r1.anim_id=197; r0.action=r1.action=109;
    r0.anim_frame=11; r1.anim_frame=1;
    out=between(r0,r1,.25f,12,false); CHECK(out.anim_frame==11); // Finite attacks never wrap.
    r0.anim_frame=4; r1.anim_frame=5;
    out=between(r0,r1,.5f,12,false); CHECK(fabsf(out.anim_frame-4.5f)<.001f);
    r1.action=0;
    out=between(r0,r1,.25f,12,false); CHECK(out.anim_frame==4 && out.action==109);
    r1.action=109; r1.anim_id=193;
    out=between(r0,r1,.25f,12,false); CHECK(out.anim_frame==4 && out.anim_id==197);
    r1.anim_id=197; r1.anim_frame=15;
    out=between(r0,r1,.25f,12,false); CHECK(out.anim_frame==4); // Reject out-of-range phases.
}

typedef struct { int n; uint8_t kind[64]; uint16_t bit[64]; } Sent;
static void record(uint8_t kind, uint16_t bit, void *user) {
    Sent *s = user;
    if (s->n < 64) { s->kind[s->n] = kind; s->bit[s->n] = bit; s->n++; }
}

static void test_flags(void) {
    puts("flags");
    uint8_t story[32] = {0}, scene[16] = {0}, mask[16];
    memset(mask, 0xff, sizeof mask);
    mask[3] = 0x0f;  // byte 3 high nibble is a counter: never shared
    story[0] = 0x01;  // already set when the save loads

    SstFlags f;
    sst_flags_init(&f);
    CHECK(sst_flags_register(&f, 1, story, sizeof story, NULL));
    CHECK(sst_flags_register(&f, 2, scene, sizeof scene, mask));
    Sent s = {0};
    sst_flags_tick(&f, record, &s);  // first tick: baseline only
    CHECK(s.n == 0);

    story[2] |= 0x10;  // game sets story bit 20
    scene[3] |= 0x31;  // bit 24 shared, bits 28/29 are counter bits
    sst_flags_tick(&f, record, &s);
    CHECK(s.n == 2);
    CHECK(s.kind[0] == 1 && s.bit[0] == 20);
    CHECK(s.kind[1] == 2 && s.bit[1] == 24);

    // local clear is never sent; remote can't clear either
    s.n = 0;
    story[2] &= ~0x10;
    sst_flags_tick(&f, record, &s);
    CHECK(s.n == 0);

    // remote apply sets the bit and does not echo back
    CHECK(sst_flags_apply(&f, 1, 100));
    CHECK(story[12] & 0x10);
    CHECK(!sst_flags_apply(&f, 1, 100));        // already set
    CHECK(!sst_flags_apply(&f, 2, 31));         // counter bit: refused
    CHECK(!(scene[3] & 0x80));
    CHECK(!sst_flags_apply(&f, 1, 32 * 8));     // out of range
    CHECK(!sst_flags_apply(&f, 7, 0));          // unregistered space
    sst_flags_tick(&f, record, &s);
    CHECK(s.n == 0);

    // loading another save takes a new baseline instead of announcing its whole contents
    memset(story, 0xff, sizeof story);
    sst_flags_rebase(&f);
    sst_flags_tick(&f, record, &s);
    CHECK(s.n == 0);

    // announce_all lists every shared set bit (masked)
    memset(story, 0, sizeof story);
    memset(scene, 0, sizeof scene);
    story[0] = 0x03;
    scene[3] = 0xff;
    sst_flags_announce_all(&f, record, &s);
    CHECK(s.n == 2 + 4);
}

static void write_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    fputs(text, fp);
    fclose(fp);
}

static void test_backup(void) {
    puts("backup");
    char root[64] = "/tmp/sst-unit-XXXXXX";
    if (!mkdtemp(root)) { CHECK(0); return; }
    char saves[256], backups[256], path[1100], out[1024], why[256];
    snprintf(saves, sizeof saves, "%s/save", root);
    snprintf(backups, sizeof backups, "%s/backups", root);
    mkdir(saves, 0755);
    snprintf(path, sizeof path, "%s/slot0.bin", saves);
    write_file(path, "slot zero");
    snprintf(path, sizeof path, "%s/slot1.bin", saves);
    write_file(path, "slot one");

    CHECK(sst_backup_saves(saves, backups, 2, out, sizeof out, why, sizeof why));
    snprintf(path, sizeof path, "%s/slot1.bin", out);
    FILE *fp = fopen(path, "rb");
    char buf[32] = {0};
    CHECK(fp && fread(buf, 1, sizeof buf, fp) == 8 && !strcmp(buf, "slot one"));
    if (fp) fclose(fp);

    // keep=2: the third backup removes the first
    char first[1024];
    snprintf(first, sizeof first, "%s", out);
    CHECK(sst_backup_saves(saves, backups, 2, out, sizeof out, why, sizeof why));
    CHECK(sst_backup_saves(saves, backups, 2, out, sizeof out, why, sizeof why));
    CHECK(access(first, F_OK) != 0);
    CHECK(access(out, F_OK) == 0);

    // missing save dir fails cleanly
    CHECK(!sst_backup_saves("/nonexistent/sst", backups, 2, NULL, 0, why, sizeof why));
    CHECK(strstr(why, "/nonexistent/sst") != NULL);

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd)) {}
}

static void test_game_read(void) {
    puts("game read (mock memory)");
    // fake "game memory": globals block and a player object, wired with real host pointers
    static uint8_t globals[64], player[0x1b00], file_mgr[0x5400];
    SstGameLayout l = {
        .player_ptr = (uint64_t)(uintptr_t)&globals[0], .stage_name = (uint64_t)(uintptr_t)&globals[16],
        .room = (uint64_t)(uintptr_t)&globals[24], .off_pos = SST_LAYOUT_SSHD_100.off_pos, .off_rot = SST_LAYOUT_SSHD_100.off_rot,
    };
    memset(globals, 0, sizeof globals);
    CHECK(!sst_game_read_local(&l, 1).valid);  // no player yet
    uint64_t pp = (uint64_t)(uintptr_t)player;
    memcpy(&globals[0], &pp, 8);
    float pos[3] = {100.5f, -20.f, 3000.f};
    int16_t rot[3] = {0, -16384, 0};
    memcpy(player + l.off_pos, pos, 12);
    memcpy(player + l.off_rot, rot, 6);
    CHECK(!sst_game_read_local(&l, 1).valid);  // no stage name
    memcpy(&globals[16], "F000", 4);
    globals[24] = 3;
    SstLocal r = sst_game_read_local(&l, 7);
    CHECK(r.valid && r.pose.tick == 7 && r.pose.pos[0] == 100.5f && r.pose.pos[2] == 3000.f && r.pose.rot[1] == -16384);
    CHECK(!strcmp(r.scene.stage, "F000") && r.scene.room == 3);
    memcpy(&globals[16], "F0000000", 8);  // full 8 chars, no terminator
    CHECK(sst_game_read_local(&l, 1).valid && !strcmp(sst_game_read_local(&l, 1).scene.stage, "F0000000"));
    float nan = NAN;
    memcpy(player + l.off_pos, &nan, 4);
    CHECK(!sst_game_read_local(&l, 1).valid);  // garbage rejected
    // the SSHD layout's offsets follow the rando struct sizes: dPlayer header 0xC8, then pos/rot
    CHECK(SST_LAYOUT_SSHD_100.off_pos == 0x144 && SST_LAYOUT_SSHD_100.off_rot == 0x13C);

    memcpy(player + l.off_pos, pos, sizeof pos);
    l.off_anim_index = SST_LAYOUT_SSHD_100.off_anim_index;
    l.off_anim_ids = SST_LAYOUT_SSHD_100.off_anim_ids;
    l.off_anim_tracks = SST_LAYOUT_SSHD_100.off_anim_tracks;
    l.anim_track_stride = SST_LAYOUT_SSHD_100.anim_track_stride;
    l.off_anim_frame = SST_LAYOUT_SSHD_100.off_anim_frame;
    l.off_anim_length = SST_LAYOUT_SSHD_100.off_anim_length;
    l.off_player_instance = SST_LAYOUT_SSHD_100.off_player_instance;
    uint32_t player_instance = 0xabcdef01;
    memcpy(player+l.off_player_instance,&player_instance,4);
    player[l.off_anim_index] = 2;
    uint16_t clip = 27;
    float anim_frame = 12.5f;
    float anim_length = 24;
    memcpy(player + l.off_anim_ids + 4, &clip, 2);
    memcpy(player + l.off_anim_tracks + 2*l.anim_track_stride + l.off_anim_frame, &anim_frame, 4);
    memcpy(player + l.off_anim_tracks + 2*l.anim_track_stride + l.off_anim_length, &anim_length, 4);
    r = sst_game_read_local(&l, 7);
    CHECK(r.valid && r.pose.anim_id == 27 && r.pose.anim_frame == 12.5f);
    CHECK(r.animation_length == 24);
    CHECK(r.player_address == pp && r.player_instance == player_instance);
    clip = 0; // Attack clip zero is valid; unavailable is UINT16_MAX.
    memcpy(player + l.off_anim_ids + 4, &clip, 2);
    CHECK(sst_game_read_local(&l, 7).pose.anim_id == 0);
    clip = 443; // Native unloaded-track sentinel.
    memcpy(player + l.off_anim_ids + 4, &clip, 2);
    CHECK(sst_game_read_local(&l, 7).pose.anim_id == UINT16_MAX);
    clip = 27;
    memcpy(player + l.off_anim_ids + 4, &clip, 2);
    anim_frame = NAN;
    memcpy(player + l.off_anim_tracks + 2*l.anim_track_stride + l.off_anim_frame, &anim_frame, 4);
    CHECK(sst_game_read_local(&l, 7).pose.anim_id == UINT16_MAX);
    player[l.off_anim_index] = 6;
    CHECK(sst_game_read_local(&l, 7).pose.anim_id == UINT16_MAX);
    player[l.off_anim_index] = 0;

    // Optional gameplay fields use independent addresses and may disappear during a load.
    memcpy(player + l.off_pos, pos, sizeof pos);
    l.layer = (uint64_t)(uintptr_t)&globals[25];
    l.entrance = (uint64_t)(uintptr_t)&globals[26];
    globals[25] = 7; globals[26] = 9;
    l.off_action = SST_LAYOUT_SSHD_100.off_action;
    uint32_t action = 17;
    memcpy(player + l.off_action, &action, sizeof action);
    l.file_mgr_ptr = (uint64_t)(uintptr_t)&globals[32];
    l.off_health = SST_LAYOUT_SSHD_100.off_health;
    l.off_max_health = SST_LAYOUT_SSHD_100.off_max_health;
    uint64_t fm = (uint64_t)(uintptr_t)file_mgr;
    memcpy(&globals[32], &fm, sizeof fm);
    uint16_t health = 20, capacity = 24;
    memcpy(file_mgr + l.off_health, &health, sizeof health);
    memcpy(file_mgr + l.off_max_health, &capacity, sizeof capacity);
    r = sst_game_read_local(&l, 8);
    CHECK(r.valid && r.scene.layer == 7 && r.scene.entrance == 9 && r.pose.action == 17);
    CHECK(r.pose.health == 20 && r.pose.max_health == 24);
    health = 100;
    memcpy(file_mgr + l.off_health, &health, sizeof health);
    r = sst_game_read_local(&l, 9);
    CHECK(r.valid && r.pose.health == 0 && r.pose.max_health == 0);

    // Life Medal bonus capacity must remain visible even when current health falls below base.
    l.off_pouch = SST_LAYOUT_SSHD_100.off_pouch;
    l.off_item_flags = SST_LAYOUT_SSHD_100.off_item_flags;
    l.off_story_flags = SST_LAYOUT_SSHD_100.off_story_flags;
    uint16_t owned_pouch = 1u << 6, medal = 114;
    memcpy(file_mgr + l.off_item_flags + 10, &owned_pouch, 2);
    memcpy(file_mgr + l.off_pouch, &medal, 2);
    health = 28; memcpy(file_mgr + l.off_health, &health, 2);
    r = sst_game_read_local(&l, 10);
    CHECK(r.pose.health == 28 && r.pose.max_health == 28);
    health = 20; memcpy(file_mgr + l.off_health, &health, 2);
    r = sst_game_read_local(&l, 10);
    CHECK(r.pose.health == 20 && r.pose.max_health == 28);
    memcpy(file_mgr + l.off_pouch + 4, &medal, 2);
    CHECK(sst_game_read_local(&l, 10).pose.max_health == 32);
    // Slots beyond the current pouch size cannot supply bonus hearts.
    memcpy(file_mgr + l.off_pouch + 16, &medal, 2);
    CHECK(sst_game_read_local(&l, 10).pose.max_health == 32);
    uint16_t expansion = 1u << 10;
    memcpy(file_mgr + l.off_item_flags + 108, &expansion, 2);
    CHECK(sst_game_read_local(&l, 10).pose.max_health == 36);
    uint16_t restricted = 1u << 5;
    memcpy(file_mgr + l.off_story_flags + 10, &restricted, 2);
    CHECK(sst_game_read_local(&l, 10).pose.max_health == 24);
    restricted = 0; memcpy(file_mgr + l.off_story_flags + 10, &restricted, 2);
    l.minigame_ptr = (uint64_t)(uintptr_t)&globals[40];
    l.minigame_mode = (uint64_t)(uintptr_t)&globals[48];
    uint64_t active_minigame = 1; uint32_t minigame_mode = 9;
    memcpy(globals + 40, &active_minigame, 8); memcpy(globals + 48, &minigame_mode, 4);
    CHECK(sst_game_read_local(&l, 10).pose.max_health == 24);
    active_minigame = 0; memcpy(globals + 40, &active_minigame, 8);
    capacity = 80; memcpy(file_mgr + l.off_max_health, &capacity, 2);
    CHECK(sst_game_read_local(&l, 10).pose.max_health == 80);

    // The title background has a real Link and may retain the previous file's health.
    memcpy(&globals[16], "F000\0\0\0\0", 8);
    globals[24] = 0; globals[25] = 28; globals[26] = 48;
    CHECK(!sst_game_read_local(&l, 10).valid);
    globals[25] = 26; globals[26] = 55;
    CHECK(!sst_game_read_local(&l, 10).valid);

    // A corrupt guest pointer must be rejected before it is dereferenced.
    l.guest_lo = SST_LAYOUT_SSHD_100.guest_lo;
    l.guest_hi = SST_LAYOUT_SSHD_100.guest_hi;
    pp = 1;
    memcpy(&globals[0], &pp, sizeof pp);
    CHECK(!sst_game_read_local(&l, 10).valid);
    pp = l.guest_hi - l.off_pos;
    memcpy(&globals[0], &pp, sizeof pp);
    CHECK(!sst_game_read_local(&l, 11).valid);
    pp = UINT64_MAX - 4;
    memcpy(&globals[0], &pp, sizeof pp);
    CHECK(!sst_game_read_local(&l, 12).valid);
}

static void test_camera(void) {
    puts("camera projection and transition validation");
    float view[12] = {1,0,0,0, 0,1,0,-100, 0,0,1,-1000}, fov=90;
    uint8_t object[256] = {0};
    memcpy(object, view, sizeof view);memcpy(object+64,&fov,4);
    uint64_t ptr = (uintptr_t)object;
    SstGameLayout l = {.camera_ptr=(uintptr_t)&ptr,.off_view=0,.off_fovy=64};
    SstCamera c;
    CHECK(sst_game_read_camera(&l,1280,720,&c));
    // The camera looks at (0,100,0): its center projects exactly to the window center.
    float x=c.view_proj[1]*100+c.view_proj[3],y=c.view_proj[5]*100+c.view_proj[7];
    float w=c.view_proj[13]*100+c.view_proj[15];
    CHECK(fabsf(x/w)<1e-5f && fabsf(y/w)<1e-5f && w==1000);
    CHECK(fabsf(c.view_proj[0] - .5625f)<1e-5f);
    CHECK(!sst_game_read_camera(&l,1280,0,&c));
    fov=NAN;memcpy(object+64,&fov,4);CHECK(!sst_game_read_camera(&l,1280,720,&c));
    fov=90;memcpy(object+64,&fov,4);memset(object,0,48);
    CHECK(!sst_game_read_camera(&l,1280,720,&c));
    ptr=0;CHECK(!sst_game_read_camera(&l,1280,720,&c));
}

static void test_parse_server(void) {
    puts("server address parsing");
    char h[128];
    uint16_t port;
    CHECK(sst_parse_server("127.0.0.1:7623", h, sizeof h, &port) && !strcmp(h, "127.0.0.1") && port == 7623);
    CHECK(sst_parse_server("example.org", h, sizeof h, &port) && !strcmp(h, "example.org") && port == 7623);
    CHECK(sst_parse_server("tcp://relay.example.org:9000", h, sizeof h, &port) && !strcmp(h, "relay.example.org") && port == 9000);
    CHECK(sst_parse_server("  100.64.0.2:1 ", h, sizeof h, &port) && !strcmp(h, "100.64.0.2") && port == 1);
    CHECK(sst_parse_server("[::1]:7000", h, sizeof h, &port) && !strcmp(h, "::1") && port == 7000);
    CHECK(sst_parse_server("::1", h, sizeof h, &port) && !strcmp(h, "::1") && port == 7623);
    CHECK(!sst_parse_server("", h, sizeof h, &port));
    CHECK(!sst_parse_server("host:0", h, sizeof h, &port));
    CHECK(!sst_parse_server("host:70000", h, sizeof h, &port));
    CHECK(!sst_parse_server("host:abc", h, sizeof h, &port));
    CHECK(!sst_parse_server(":7623", h, sizeof h, &port));
}

static void test_settings(void) {
    puts("settings round trip");
    char path[] = "/tmp/sst-settings-XXXXXX";
    int fd = mkstemp(path);
    FILE *f = fdopen(fd, "w+");
    snprintf(g_sst.name, sizeof g_sst.name, "Jo");
    snprintf(g_sst.server, sizeof g_sst.server, "relay.example.org:9000");
    g_sst.auto_reconnect = false;
    g_sst.use_udp = true;
    g_sst.host_port = 9001;
    g_sst.show_avatars = true;
    g_sst.sync_enemy_motion = true;
    sst_settings_save(f);
    rewind(f);
    memset(&g_sst, 0, sizeof g_sst);
    char line[256];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        char *eq = strstr(line, " = ");
        if (!eq) continue;
        *eq = 0;
        CHECK(sst_settings_load(line, eq + 3));
    }
    fclose(f);
    unlink(path);
    CHECK(!strcmp(g_sst.name, "Jo") && !strcmp(g_sst.server, "relay.example.org:9000") && !g_sst.auto_reconnect);
    CHECK(g_sst.host_port == 9001 && g_sst.show_avatars);
    CHECK(g_sst.sync_enemy_motion);
    CHECK(g_sst.use_udp);
    CHECK(!sst_settings_load("fullscreen", "1"));
}

int main(void) {
    test_pose_buffer();
    test_flags();
    test_backup();
    test_game_read();
    test_camera();
    test_parse_server();
    test_settings();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}

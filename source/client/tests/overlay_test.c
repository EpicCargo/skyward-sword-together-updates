// Unit tests for the overlay's world-to-screen projection and drawing logic.
// Stubs ImGui drawing calls to capture what would be drawn.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define SST_OVERLAY_TEST
#include "../src/sst_overlay.h"
#define sst_game_local overlay_local
#define sst_remote_network_sample overlay_sample
#include "../src/sst_overlay.c"
#undef sst_game_local
#undef sst_remote_network_sample
#include "../src/sst_avatar.h"
#ifdef SST_OVERLAY_STANDALONE
SstSettings g_sst;
#endif

static SstLocal test_local;
static SstRemote test_peer;
static int test_peers;
SstLocal overlay_local(void) { return test_local; }
int overlay_sample(const SstScene *scene, SstRemote *out, int capacity) {
    assert(!strcmp(scene->stage, test_local.scene.stage) && scene->room == test_local.scene.room && capacity >= test_peers);
    if (test_peers) out[0] = test_peer;
    return test_peers;
}

// --- drawing stubs ---

#define MAX_DRAWS 64

typedef struct {
    enum { DRAW_TEXT, DRAW_RECT } type;
    float x, y, w, h;
    unsigned col;
    char text[128];
} DrawCall;

static DrawCall s_draws[MAX_DRAWS];
static int s_draw_count;

void sst_draw_text(float x, float y, unsigned col, const char *text) {
    if (s_draw_count >= MAX_DRAWS) return;
    DrawCall *d = &s_draws[s_draw_count++];
    d->type = DRAW_TEXT;
    d->x = x; d->y = y; d->col = col;
    snprintf(d->text, sizeof d->text, "%s", text);
}

void sst_draw_rect_filled(float x, float y, float w, float h, unsigned col) {
    if (s_draw_count >= MAX_DRAWS) return;
    DrawCall *d = &s_draws[s_draw_count++];
    d->type = DRAW_RECT;
    d->x = x; d->y = y; d->w = w; d->h = h; d->col = col;
}

static void reset_draws(void) { s_draw_count = 0; }

// --- projection tests ---

static void identity_vp(float vp[16]) {
    memset(vp, 0, 16 * sizeof(float));
    vp[0] = 1; vp[5] = 1; vp[10] = 1; vp[15] = 1;
}

static void test_projection_center(void) {
    float vp[16]; identity_vp(vp);
    float sx, sy;
    int vis = world_to_screen(vp, 1280, 720, (float[]){0, 0, 0}, &sx, &sy);
    assert(vis);
    assert(fabsf(sx - 640.0f) < 1.0f);
    assert(fabsf(sy - 360.0f) < 1.0f);
    printf("  projection_center: ok\n");
}

static void test_projection_behind(void) {
    float vp[16]; identity_vp(vp);
    // point behind camera: w <= 0 after transform (set w-row to make w negative)
    vp[12] = 0; vp[13] = 0; vp[14] = -1; vp[15] = 0;
    float sx, sy;
    int vis = world_to_screen(vp, 1280, 720, (float[]){0, 0, 1}, &sx, &sy);
    assert(!vis); // behind camera
    printf("  projection_behind: ok\n");
}

static void test_projection_offscreen(void) {
    float vp[16]; identity_vp(vp);
    float sx, sy;
    int vis = world_to_screen(vp, 1280, 720, (float[]){2.0f, 0, 0}, &sx, &sy);
    assert(!vis); // x/w = 2.0, outside [-1.2, 1.2]
    printf("  projection_offscreen: ok\n");
}

// --- overlay draw tests (mocked network) ---

// Supply the render snapshot directly; exercise the actual overlay drawing path.

static void test_draw_stubs(void) {
    reset_draws();
    sst_draw_text(100, 200, 0xFFFFFFFF, "Hello");
    sst_draw_rect_filled(10, 20, 50, 8, 0xFF00CC44);
    assert(s_draw_count == 2);
    assert(s_draws[0].type == DRAW_TEXT);
    assert(s_draws[0].x == 100.0f);
    assert(strcmp(s_draws[0].text, "Hello") == 0);
    assert(s_draws[1].type == DRAW_RECT);
    assert(s_draws[1].w == 50.0f);
    printf("  draw_stubs: ok\n");
}

static void test_no_draw_when_hidden(void) {
    reset_draws();
    g_sst.show_names = false;
    SstCamera cam = {.screen_w = 1280, .screen_h = 720};
    identity_vp(cam.view_proj);
    sst_overlay_draw(&cam);
    assert(s_draw_count == 0);
    g_sst.show_names = true;
    printf("  no_draw_when_hidden: ok\n");
}

static void test_no_draw_null_camera(void) {
    reset_draws();
    sst_overlay_draw(NULL);
    assert(s_draw_count == 0);
    printf("  no_draw_null_camera: ok\n");
}

static void test_carry_clearance(void) {
    test_local = (SstLocal){.valid=true, .pose={.max_health=24}};
    test_peer = (SstRemote){.id=2, .name="Carrier", .pose={.health=24, .max_health=24}};
    test_peers = 1;
    const unsigned flags[] = {0, SST_EQUIP_SWORD_DRAWN, SST_POSE_CONTEXT_SMALL_CARRY,
        SST_POSE_CONTEXT_SMALL_CARRY|SST_EQUIP_SWORD_DRAWN, SST_POSE_CONTEXT_SMALL_CARRY|0xe1,
        SST_POSE_CONTEXT_UNSUPPORTED};
    for (unsigned screen=0; screen<3; screen++) for (unsigned i=0; i<sizeof flags/sizeof *flags; i++) {
        SstCamera camera={.screen_w=640.f*(screen+1), .screen_h=360.f*(screen+1)};
        identity_vp(camera.view_proj); camera.view_proj[5]=1.f/400;
        test_peer.pose.equip_flags=(uint8_t)flags[i]; reset_draws(); sst_overlay_draw(&camera);
        assert(s_draw_count==3 && s_draws[0].type==DRAW_TEXT && s_draws[1].type==DRAW_RECT);
        float x, rim;
        assert(world_to_screen(camera.view_proj,camera.screen_w,camera.screen_h,(float[]){0,185.5f,0},&x,&rim));
        if ((flags[i]&SST_POSE_CONTEXT_MASK)==SST_POSE_CONTEXT_SMALL_CARRY) {
            // Native small-pot rim around185 game units: even the bar's bottom
            // must sit above that projected volume, with visible pixel clearance.
            assert(s_draws[1].y+s_draws[1].h < rim-8.f);
        } else {
            float ordinary;
            assert(world_to_screen(camera.view_proj,camera.screen_w,camera.screen_h,(float[]){0,180,0},&x,&ordinary));
            assert(fabsf(s_draws[1].y-(ordinary-1))<.001f);
        }
    }
    test_peer.pose.pos[0]=10; reset_draws();
    SstCamera camera={.screen_w=640,.screen_h=360};identity_vp(camera.view_proj);camera.view_proj[5]=1.f/400;
    sst_overlay_draw(&camera);assert(!s_draw_count);
    test_local.valid=false;test_peer.pose.pos[0]=0;reset_draws();sst_overlay_draw(&camera);assert(!s_draw_count);
    test_peers=0;
    puts("  carry clearance:18 flag/resolution cases, offscreen and invalid-local controls passed");
}

int main(void) {
    g_sst.show_names = true;
    printf("overlay tests:\n");
    test_projection_center();
    test_projection_behind();
    test_projection_offscreen();
    test_draw_stubs();
    test_no_draw_when_hidden();
    test_no_draw_null_camera();
    test_carry_clearance();
    printf("overlay: 6 original checks plus20 carry/visibility contracts passed\n");
    return 0;
}

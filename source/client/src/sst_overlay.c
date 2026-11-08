// See sst_overlay.h. Pure C; the ImGui calls are behind a thin interface so the overlay logic can
// be unit-tested without linking ImGui.
#include "sst_overlay.h"
#include "sst_net.h"
#include "sst_game.h"
#include "sst_remote.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

// --- world-to-screen projection ---

static int world_to_screen(const float vp[16], float sw, float sh,
                           const float world[3], float *sx, float *sy) {
    float x = world[0], y = world[1], z = world[2];
    float cx = vp[0]*x + vp[1]*y + vp[2]*z + vp[3];
    float cy = vp[4]*x + vp[5]*y + vp[6]*z + vp[7];
    float cz = vp[8]*x + vp[9]*y + vp[10]*z + vp[11];
    float cw = vp[12]*x + vp[13]*y + vp[14]*z + vp[15];
    if (!isfinite(cx) || !isfinite(cy) || !isfinite(cw) || cw <= 0.001f) return 0; // behind camera
    float ndx = cx / cw, ndy = cy / cw;
    *sx = (ndx * 0.5f + 0.5f) * sw;
    *sy = (1.0f - (ndy * 0.5f + 0.5f)) * sh; // Y flipped
    if (ndx < -1.2f || ndx > 1.2f || ndy < -1.2f || ndy > 1.2f) return 0;
    (void)cz;
    return 1;
}

// --- drawing interface (weak symbols so tests can stub them) ---

#ifdef SST_OVERLAY_TEST
// stubs filled by the test
void sst_draw_text(float x, float y, unsigned col, const char *text);
void sst_draw_rect_filled(float x, float y, float w, float h, unsigned col);
#else

#ifdef __cplusplus
extern "C" {
#endif
// Implemented in the engine's ImGui overlay pass (multiplayer_overlay.mm).
void sst_draw_text(float x, float y, unsigned col, const char *text);
void sst_draw_rect_filled(float x, float y, float w, float h, unsigned col);
#ifdef __cplusplus
}
#endif
#endif

// --- the draw call ---

#define TAG_OFFSET_Y 180.0f // game units above the player's feet
#define BAR_W 60.0f
#define BAR_H 6.0f
#define COL_WHITE 0xFFFFFFFF
#define COL_BLACK 0xFF000000
#define COL_GREEN 0xFF00CC44
#define COL_RED   0xFF2222DD
#define COL_BG    0xAA000000

void sst_overlay_draw(const SstCamera *camera) {
    if (!camera || !g_sst.show_names) return;

    SstLocal local = sst_game_local();
    if (!local.valid || !local.pose.max_health) return;
    SstRemote remotes[SST_MAX_PLAYERS];
    int n = sst_remote_network_sample(&local.scene, remotes, SST_MAX_PLAYERS);
    for (int i = 0; i < n; i++) {
        SstPose smooth = remotes[i].pose;

        // Project the position (elevated to head height)
        float world[3] = {smooth.pos[0], smooth.pos[1] + TAG_OFFSET_Y, smooth.pos[2]};
        float sx, sy;
        if (!world_to_screen(camera->view_proj, camera->screen_w, camera->screen_h, world, &sx, &sy))
            continue;

        // Name tag
        char label[SST_STR + 8];
        snprintf(label, sizeof label, "%s", remotes[i].name[0] ? remotes[i].name : "Player");
        float tw = (float)strlen(label) * 7.0f; // approximate character width
        sst_draw_text(sx - tw * 0.5f, sy - 18.0f, COL_WHITE, label);

        // Health bar background
        float bx = sx - BAR_W * 0.5f;
        float by = sy;
        sst_draw_rect_filled(bx - 1, by - 1, BAR_W + 2, BAR_H + 2, COL_BG);

        // Health bar fill
        if (smooth.max_health > 0) {
            float frac = (float)smooth.health / (float)smooth.max_health;
            if (frac > 1.0f) frac = 1.0f;
            unsigned col = frac > 0.3f ? COL_GREEN : COL_RED;
            sst_draw_rect_filled(bx, by, BAR_W * frac, BAR_H, col);
        }
    }
}

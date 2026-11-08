// In-game overlay: name tags and health bars drawn over remote players.
// Called from the engine's ImGui render pass. The overlay projects each remote player's world
// position to screen space and draws a floating label above them.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float view_proj[16]; // row-major 4x4 view-projection matrix
    float screen_w, screen_h;
} SstCamera;

// Draws name tags and health bars for all connected remote players.
// camera may be NULL (skips drawing). Called once per frame from the ImGui overlay pass.
void sst_overlay_draw(const SstCamera *camera);

#ifdef __cplusplus
}
#endif

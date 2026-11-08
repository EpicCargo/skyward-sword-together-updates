// Render-thread snapshot shared by the world renderer and labels.
#pragma once
#include "sst_net.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint16_t id;
    char name[SST_STR];
    SstPose pose;
    uint32_t presence_generation; // local incarnation, never a wire field
} SstRemote;
// Drop stale/invalid poses, reset interpolation on scene changes and reclaim departed slots.
int sst_remote_sample(const SstScene *local, const SstPlayer *players, int count, double now_ms,
                      SstRemote *out, int capacity);
void sst_remote_reset(void);
// Render thread: update once per presented-frame identity, independently of
// avatar geometry and name labels. NULL scene clears the snapshot. After this
// lifecycle starts, network_sample only copies this frame's immutable result.
void sst_remote_frame(const SstScene *local, uint64_t frame);
int sst_remote_network_sample(const SstScene *local, SstRemote *out, int capacity);
// Immutable copy of the render thread's most recent interpolated pose. Safe on
// the guest thread; never advances or reads the mutable interpolation buffers.
bool sst_remote_displayed(const SstScene *scene, uint16_t id, uint32_t presence,
                          SstPose *out);
// Renderer receipt for an actually emitted skinned carry body. NULL rendered
// invalidates a previously drawn carry when replay becomes unavailable/neutral.
// A prepared/network pose alone cannot authorize a held native object.
void sst_remote_carry_drawn(const SstScene *scene, const SstRemote *source,
                           const SstPose *rendered);
bool sst_remote_carry_displayed(const SstScene *scene, uint16_t id, uint32_t presence,
                              SstPose *out);
#ifdef __cplusplus
}
#endif

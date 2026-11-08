// Bounded local library of observed locomotion, sprint, roll, jump and sword poses.
// No assets or palettes go on wire.
#pragma once
#include "sst_net.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SST_ANIMATION_BUDGET (8u * 1024u * 1024u)
typedef struct SstAnimationSample SstAnimationSample;
typedef struct {
    const SstAnimationSample *before, *after;
    float blend;
    bool attachment; // model-only replay from a verified cached body palette joint
    uint16_t joint;
    bool child; // validated joint-local hand offset, held by value
    float local[12];
} SstAnimationPose;
typedef struct {
    SstPose pose; // original placement; animation may be a verified static idle snapshot
    uint64_t cohort; // zero: exact; nonzero: one complete native ordinary/carry idle frame
    bool ready;
} SstAnimationReplay;
typedef struct {
    uint64_t exact_frames, neutral_frames, unavailable_frames, suppressed_parts;
    size_t neutral_parts;
    uint64_t carry_frames;
    size_t carry_parts;
} SstAnimationReplayStats;
typedef struct {
    size_t bytes, samples;
    uint64_t captures, hits, misses;
    uint64_t sword_captures, sword_hits;
    uint64_t resumes; // verified geometry reused after an area/player transition
    uint64_t attachments, attachment_hits;
    uint64_t rigid_skips; // unverified rigid pieces hidden while an independent body pose is available
    uint64_t skinned_hits, skinned_misses;
} SstAnimationStats;
typedef struct { uint64_t hits, misses; } SstAnimationPartStats;

void sst_animation_reset(void);
// Begin the render frame before capturing; repeated calls within a frame are ignored. An absent
// player pauses capture. Canonical copies survive area/player transitions within the 8 MiB cap;
// reset explicitly on reboot or disabling avatars. Allow native clip transitions to settle first.
void sst_animation_frame(const SstScene *scene, const SstPose *local, uint64_t frame);
// Explicit source provenance on the render thread. false keeps real metadata but
// permits geometry declaration/replay ONLY. Denial is sticky for this presented
// frame, even if a later natural call requests true. Mode transitions discard
// unfinished observations/cohorts and restart settling, retaining learned data.
// The caller must pass false for a forced record even after leaving first person.
void sst_animation_source_frame(const SstScene *scene, const SstPose *local,
                                uint64_t frame, bool allow_learning);
// Local native clip duration permits interpolation across loop boundaries. Call after frame().
// Rejected while source learning is denied; no clip/hand cache invalidation.
bool sst_animation_set_length(uint16_t clip, float length);
// Returns locally verified duration/loop metadata for supported clips, if observed this scene.
bool sst_animation_clip(uint16_t clip, float *length, bool *loop);
// Declare already authorized geometry/layout only; no palette/model/pose reads,
// attachment observation, settling, neutral capture or equipment inference.
// Equipment identities55..60 do not join the body manifest. Overflow fails the
// manifest closed; true includes an already declared part.
bool sst_animation_declare_part(uint16_t mesh, size_t position_size, size_t normal_size);
// mesh must identify verified Link geometry. The model matrix and palettes are in view space.
bool sst_animation_capture(uint16_t mesh, const SstPose *local, const float view[12],
                           const float model[12], const void *positions, size_t position_size,
                           const void *normals, size_t normal_size);
// Samples remain valid until the next capture/reset. Exact palette sizes prevent mixing layouts.
bool sst_animation_select(uint16_t mesh, const SstPose *remote, size_t position_size,
                           size_t normal_size, SstAnimationPose *out);
// Prepare once per peer/render frame. All previously observed skinned parts must have the remote
// animation, otherwise safe identity4 requests can use a complete same-scene/layout native
// carry-idle26/action67 snapshot. Its carry identity is preserved. Other misses use one
// settled ordinary idle source frame. Carry snapshots expire on scene/entrance/load gaps;
// unsupported/drawn/unsafe carry cannot use them. A completely cold cache is
// unavailable until verified local idle geometry has been observed; never borrow viewer motion.
bool sst_animation_replay_prepare(const SstPose *remote, SstAnimationReplay *out);
bool sst_animation_replay_select(uint16_t mesh, const SstAnimationReplay *replay,
                                size_t position_size, size_t normal_size, SstAnimationPose *out);
SstAnimationReplayStats sst_animation_replay_stats(void);
// A missing rigid pose cannot reuse a different local pose when a cached remote body is playing.
bool sst_animation_skip_rigid(const SstPose *local, const SstPose *remote);
// Overrides only the model matrix (buffer 0) or palettes (3,4) in an existing uniform copy.
bool sst_animation_uniform(const SstAnimationPose *sample, const SstPose *remote,
                           const float view[12], unsigned buffer, void *uniform, size_t size);
SstAnimationStats sst_animation_stats(void);
// Bounded per-pose-identity replay evidence; aggregate statistics retain their existing ABI.
SstAnimationPartStats sst_animation_part_stats(uint16_t mesh);
// Read-only census evidence: matching verified per-clip wrist attachments in at
// least two clips. This diagnostic never authorizes a draw or cross-clip pose.
bool sst_animation_wrist_link(uint16_t mesh, uint16_t *body, uint16_t *joint, uint32_t *positions);

#ifdef __cplusplus
}
#endif

// Render producer only: diagnostic hash excludes LRU/stats/unfinished teaching.
uint64_t sst_animation_teaching_hash(void);

// Playout buffer for one remote player's poses: smooths network jitter by rendering a little in the
// past and interpolating between received samples. Pure logic, no game or network dependencies.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "sst_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SST_POSE_RING 64
#define SST_TICK_MS (1000.0 / 30.0)
#define SST_CADENCE_RING 32
typedef struct { int64_t tick; double ms; } SstPoseArrival;

typedef struct {
    SstPose ring[SST_POSE_RING];
    double recv_ms[SST_POSE_RING];
    int64_t timeline[SST_POSE_RING]; // unwrapped observation IDs; wire uint32 remains unchanged
    int count, head;          // head = index of the newest sample
    double clock_tick;        // unwrapped remote ordinal currently shown (fractional)
    double delay_ticks;       // how far behind the newest sample we play, 2..8, adapts to jitter
    double jitter_ms;         // smoothed |arrival interval - expected|
    double last_update_ms;
    bool started;
    uint16_t animation_clip;
    float animation_length;
    bool animation_loop;
    double source_period_ms; // robust estimated time per ordinal source tick
    SstPoseArrival cadence[SST_CADENCE_RING];
    unsigned cadence_head, cadence_count;
    bool cadence_processed;
    int64_t processed_tick;
    double processed_ms;
} SstPoseBuffer;

void sst_pose_buffer_reset(SstPoseBuffer *b);
// Adds a received pose; late samples are ordered, duplicates dropped. A new forward
// observation after >1s receipt silence resets stale history and cadence evidence.
void sst_pose_buffer_push(SstPoseBuffer *b, const SstPose *p, double now_ms);
// Optional locally observed clip metadata. Zero length clears it. Never infer a loop from a restart.
void sst_pose_buffer_animation(SstPoseBuffer *b, uint16_t clip, float length, bool loop);
// Advances the playout clock to now_ms and writes the pose to draw. false: nothing to draw yet.
bool sst_pose_buffer_sample(SstPoseBuffer *b, double now_ms, SstPose *out);

#ifdef __cplusplus
}
#endif

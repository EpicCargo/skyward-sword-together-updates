// Session-only developer controls. UI queues intent; only the game thread mutates the player.
#pragma once
#include "sst_net.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { bool moon_jump, noclip; float flight_speed; } SstDevControls;
typedef enum { SST_DEV_NONE, SST_DEV_DESTINATION, SST_DEV_PEER } SstDevRequestKind;
typedef struct {
    SstDevRequestKind kind; size_t destination; uint16_t peer; uint32_t serial;
    uint32_t connection_generation,presence_generation,peer_scene_generation;
    uint64_t queued_ms;
    SstScene peer_scene;
} SstDevRequest;
typedef struct {
    bool available, busy;
    uint32_t completed, rejected;
    char message[192];
} SstDevStatus;
SstDevControls sst_dev_controls(void);
void sst_dev_set_controls(SstDevControls controls);
bool sst_dev_queue_destination(size_t index);
bool sst_dev_queue_peer(uint16_t peer);
bool sst_dev_take_request(SstDevRequest *out);
SstDevStatus sst_dev_status(void);
void sst_dev_report(bool available, bool busy, const char *message);
void sst_dev_finish(bool success, const char *message);
void sst_dev_reset(void);
// Camera-relative free-flight displacement in world units. No guest pointers or writes.
bool sst_dev_flight_delta(const float view[12], int32_t x, int32_t y,
                         bool up, bool down, bool boost, float speed, double seconds, float out[3]);
#ifdef __cplusplus
}
#endif

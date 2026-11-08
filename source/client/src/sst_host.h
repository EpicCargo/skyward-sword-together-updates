// Player-hosted session. The hosting game joins this listener over loopback; three friends
// connect to the host's address. No separate server process is needed.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SST_COOP_PLAYERS 4
// port=0 chooses an available port. Binds IPv4 on all interfaces unless SST_HOST_BIND is provided.
bool sst_host_start(uint16_t port, const char *room, char *why, size_t why_size);
void sst_host_stop(void);
uint16_t sst_host_port(void); // zero when stopped
const char *sst_host_address(void); // bound IPv4 address, empty when stopped; inspect with hosting UI

#ifdef __cplusplus
}
#endif

// No game memory, assets, save data or internet endpoints are used here.
#include "../src/sst_game.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#include <time.h>

static void check_preferences(void) {
    assert(!strcmp(g_sst.team, "saved-team"));
    assert(!strcmp(g_sst.room, "saved-room"));
    assert(!strcmp(g_sst.server, "saved.invalid:7623"));
    assert(g_sst.use_udp);
}

static void start(const char *lease) {
    setenv("SST_AUTOCONNECT", "1", 1);
    setenv("SST_AUTOHOST", "0", 1);
    setenv("SST_NO_PLAYER_READ", "1", 1);
    setenv("SST_SESSION_SERVER", "127.0.0.1:1", 1);
    setenv("SST_SESSION_ROOM", "ABC234", 1);
    setenv("SST_SESSION_ROLE", "guest", 1);
    setenv("SST_SESSION_LEASE_FILE", lease, 1);
    sst_game_reset();
    sst_frame(1);
}

static void check_offline(void) {
    char code[7], why[256]; bool creator = true;
    assert(!sst_game_session(code, sizeof code, &creator));
    assert(!code[0] && !creator);
    assert(!strcmp(sst_game_team(), "saved-team"));
    assert(sst_net_status(why, sizeof why) == SST_OFFLINE);
    assert(strstr(why, "online room ended"));
    check_preferences();
}

int main(void) {
    strcpy(g_sst.team, "saved-team");
    strcpy(g_sst.room, "saved-room");
    strcpy(g_sst.server, "saved.invalid:7623");
    g_sst.use_udp = true;
    char lease[] = "/tmp/sst-lease-XXXXXX";
    int fd = mkstemp(lease); assert(fd >= 0); close(fd);
    start(lease);
    char code[7]; bool creator = true;
    assert(sst_game_session(code, sizeof code, &creator));
    assert(!strcmp(code, "ABC234") && !creator);
    assert(!strcmp(sst_game_team(), ""));
    check_preferences();
    unlink(lease);
    usleep(300000);
    sst_frame(2);
    check_offline();

    fd = creat(lease, 0600); assert(fd >= 0); close(fd);
    struct utimbuf stale = {time(NULL) - 60, time(NULL) - 60};
    assert(!utime(lease, &stale));
    usleep(300000);
    start(lease);
    check_offline();
    sst_game_reset(); unlink(lease);
    puts("session lifecycle: transient team, preference preservation, leave and stale lease passed");
    return 0;
}

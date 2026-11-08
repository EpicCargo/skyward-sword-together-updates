// A live socket replacement keeps the peer ID, but starts a fresh movement stream.
#include "../src/sst_host.h"
#include "../src/sst_net.h"
#include "../src/sst_remote.h"
#include "../src/sst_enemy_motion.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const SstScene area = {.stage = "F001r", .room = 1, .layer = 1, .entrance = 5};
static void exact(int fd, void *data, size_t size) {
    unsigned char *p = data;
    while (size) {
        ssize_t n = recv(fd, p, size, 0); assert(n > 0);
        p += n; size -= (size_t)n;
    }
}
static void send_frame(int fd, unsigned char type, const void *body, unsigned size) {
    unsigned char data[512]; unsigned length = size + 1;
    assert(size + 5 <= sizeof data);
    memcpy(data, &length, 4); data[4] = type; memcpy(data + 5, body, size);
    assert(send(fd, data, size + 5, MSG_NOSIGNAL) == (ssize_t)size + 5);
}
static void str(unsigned char *data, unsigned *size, const char *value) {
    unsigned n = (unsigned)strlen(value); data[(*size)++] = (unsigned char)n;
    memcpy(data + *size, value, n); *size += n;
}
static int join(unsigned port, const char *name, unsigned *id) {
    int fd = socket(AF_INET, SOCK_STREAM, 0); assert(fd >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = htons(port)};
    assert(!connect(fd, (void *)&address, sizeof address));
    struct timeval timeout = {.tv_sec = 3};
    assert(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout));
    unsigned char body[256] = {SST_PROTO & 255, SST_PROTO >> 8}; unsigned n = 2;
    str(body, &n, name); str(body, &n, "coop"); str(body, &n, "team");
    memset(body + n, 42, 16); n += 16; send_frame(fd, 1, body, n);
    unsigned length; unsigned char type;
    exact(fd, &length, 4); exact(fd, &type, 1);
    assert(type == 2 && length >= 4 && length <= sizeof body);
    exact(fd, body, length - 1); *id = body[0] | (unsigned)body[1] << 8;
    return fd;
}
static void pose(int fd, uint32_t tick, float x) {
    unsigned char body[36] = {0}; const float y = 1.5f, z = -2700;
    memcpy(body, &tick, 4); memcpy(body + 4, &x, 4); memcpy(body + 8, &y, 4); memcpy(body + 12, &z, 4);
    body[28] = 2; body[30] = body[32] = 24; body[34] = SST_MOTION_CAPABLE;
    send_frame(fd, 0x11, body, sizeof body);
}
static void scene(int fd) {
    unsigned char body[32]; unsigned n = 0;
    str(body, &n, area.stage); body[n++] = area.room; body[n++] = area.layer; body[n++] = area.entrance;
    send_frame(fd, 0x10, body, n);
}
static SstPlayer wait_player(const char *name, int has_pose, uint32_t tick) {
    uint64_t deadline = sst_now_ms() + 2000;
    while (sst_now_ms() < deadline) {
        SstPlayer players[SST_MAX_PLAYERS]; int n = sst_net_players(players, SST_MAX_PLAYERS);
        for (int i = 0; i < n; i++)
            if (!strcmp(players[i].name, name) && players[i].has_pose == has_pose &&
                (!has_pose || players[i].pose.tick == tick)) return players[i];
        usleep(1000);
    }
    fprintf(stderr, "No fresh presence for %s (pose=%d tick=%u)\n", name, has_pose, tick);
    assert(!"rejoined player retained old presence or rejected a restarted tick");
    return (SstPlayer){0};
}
static SstEnemyEvent last_enemy;
static SstEnemyMotion last_motion;
static unsigned event_count, motion_count;
static void enemy_event(const SstEnemyEvent *e, void *u) { (void)u; last_enemy=*e; event_count++; }
static void motion_event(const SstEnemyMotion *m, void *u) { (void)u; last_motion=*m; motion_count++; }
static void events(int fd, uint32_t sequence, float x) {
    unsigned char body[128]; unsigned n=0;
    str(body, &n, area.stage); body[n++]=area.room; body[n++]=area.layer;
    uint64_t key=sst_world_enemy_key(0x16d, 12, 0x1234); uint16_t profile=0x16d, damage=2, hp=18;
    memcpy(body+n,&key,8); n+=8; memcpy(body+n,&sequence,4); n+=4;
    memcpy(body+n,&profile,2); n+=2; memcpy(body+n,&damage,2); n+=2; memcpy(body+n,&hp,2); n+=2;
    body[n++]=0; send_frame(fd,0x30,body,n);
    n=0; str(body,&n,area.stage); body[n++]=area.room; body[n++]=area.layer;
    memcpy(body+n,&key,8); n+=8; memcpy(body+n,&sequence,4); n+=4; memcpy(body+n,&profile,2); n+=2;
    float pos[3]={x,20,300}, velocity[3]={0}; int16_t rot[3]={0};
    memcpy(body+n,pos,12); n+=12; memcpy(body+n,rot,6); n+=6; memcpy(body+n,velocity,12); n+=12;
    send_frame(fd,0x31,body,n);
}
static void wait_events(unsigned total) {
    uint64_t deadline=sst_now_ms()+2000;
    do {
        sst_net_poll_enemies(enemy_event,NULL); sst_net_poll_motion(motion_event,NULL);
        if(event_count>=total && motion_count>=total) return;
        usleep(1000);
    } while(sst_now_ms()<deadline);
    assert(!"enemy stream did not resume after same-ID rejoin");
}
int main(int argc, char **argv) {
    assert(argc<=2);
    bool embedded=argc==1;
    if(embedded) { char why[160]; assert(sst_host_start(0, "coop", why, sizeof why)); }
    unsigned port=embedded?sst_host_port():(unsigned)atoi(argv[1]);
    unsigned original_id, replacement_id;
    int original = join(port, "Old source", &original_id); scene(original); pose(original, 50, 100);
    SstConfig config = {.host = "127.0.0.1", .port = port, .room = "coop", .team = "team", .name = "Observer"};
    sst_net_start(&config); sst_net_send_scene(&area);
    SstPlayer before = wait_player("Old source", 1, 50);
    SstRemote remote[4];
    assert(sst_remote_network_sample(&area, remote, 4) == 1 && remote[0].pose.pos[0] == 100);
    events(original,50,100); wait_events(1);
    assert(last_enemy.presence_generation==before.presence_generation);
    assert(last_motion.presence_generation==before.presence_generation);
    static SstEnemies enemies={0}; sst_enemies_begin(&enemies,&area,1);
    sst_enemies_observe(&enemies,last_enemy.key,1,last_enemy.profile,20,NULL,NULL);
    assert(sst_enemies_receive(&enemies,&last_enemy)->health==18);
    SstMotionState movement={0}; sst_motion_begin(&movement,&area,1,original_id);
    sst_motion_observe(&movement,last_motion.key,1,last_motion.profile);
    assert(sst_motion_receive(&movement,&last_motion,sst_now_ms()));

    int replacement = join(port, "Returning source", &replacement_id);
    assert(original_id == replacement_id);
    // No render sample between JOIN and the new POSE: packets can arrive in one network batch.
    wait_player("Returning source", 0, 0);
    scene(replacement); pose(replacement, 1, 200);
    SstPlayer after = wait_player("Returning source", 1, 1);
    assert(after.id == before.id);
    assert(after.presence_generation!=before.presence_generation);
    assert(sst_remote_network_sample(&area, remote, 4) == 1 && remote[0].pose.pos[0] == 200);
    events(replacement,1,200); wait_events(2);
    assert(last_enemy.presence_generation==after.presence_generation);
    assert(last_motion.presence_generation==after.presence_generation);
    assert(sst_enemies_receive(&enemies,&last_enemy)->health==16);
    assert(sst_motion_receive(&movement,&last_motion,sst_now_ms()));
    float position[3],velocity[3]; int16_t rotation[3];
    assert(sst_motion_sample(&movement,last_motion.key,1,sst_now_ms(),position,rotation,velocity) && position[0]==200);

    // A departure/rejoin also resets interpolation even when no render sees the absent player.
    close(replacement); usleep(100000);
    replacement = join(port, "Returned again", &replacement_id);
    if (embedded) assert(original_id == replacement_id);
    scene(replacement); pose(replacement, 0, 250); wait_player("Returned again", 1, 0);
    assert(sst_remote_network_sample(&area, remote, 4) == 1 && remote[0].pose.pos[0] == 250);
    close(original); close(replacement); sst_net_stop(); if(embedded) sst_host_stop();
    puts("rejoin: same-ID replacement, stale presence, restarted poses, enemy damage/movement and interpolation reset PASS");
}

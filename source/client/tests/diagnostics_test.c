// Exercise public diagnostics against a delayed, deliberately malformed heartbeat server.
#include "../src/sst_net.h"
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static atomic_int phase;
static int listener;
static void exact(int fd, void *out, size_t n) {
    unsigned char *p = out;
    while (n) { ssize_t got = recv(fd, p, n, 0); assert(got > 0); p += got; n -= (size_t)got; }
}
static void send_frame(int fd, unsigned char type, const void *body, unsigned n) {
    unsigned len = n + 1; unsigned char b[64]; assert(n < 59);
    memcpy(b, &len, 4); b[4] = type; if (n) memcpy(b + 5, body, n);
    assert(send(fd, b, n + 5, MSG_NOSIGNAL) == n + 5);
}
static void *server(void *unused) {
    (void)unused;
    for (int generation = 0; generation < 2; generation++) {
        int fd = accept(listener, NULL, NULL); assert(fd >= 0);
        struct timeval timeout = {.tv_sec = 3};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        uint32_t token = 0;
        for (;;) {
            unsigned n; unsigned char body[512], type;
            exact(fd, &n, 4); assert(n && n <= sizeof body); exact(fd, &type, 1);
            if (n > 1) exact(fd, body, n - 1);
            if (type == 1) { const unsigned char welcome[] = {1, 0, 0}; send_frame(fd, 2, welcome, 3); }
            if (type == 6) { assert(n == 5); memcpy(&token, body, 4); break; }
        }
        uint32_t wrong = token ^ 1;
        send_frame(fd, 7, &token, 3); send_frame(fd, 7, &wrong, 4);
        atomic_store(&phase, 1 + generation * 4);
        while (atomic_load(&phase) < 2 + generation * 4) usleep(1000);
        usleep(120000); send_frame(fd, 7, &token, 4);
        atomic_store(&phase, 3 + generation * 4);
        while (atomic_load(&phase) < 4 + generation * 4) usleep(1000);
        close(fd);
    }
    return NULL;
}
static void wait_phase(int target) {
    uint64_t until = sst_now_ms() + 3000;
    while (atomic_load(&phase) < target && sst_now_ms() < until) usleep(1000);
    assert(atomic_load(&phase) >= target);
}
int main(void) {
    listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(!bind(listener, (void *)&address, sizeof address) && !listen(listener, 2));
    socklen_t size = sizeof address; assert(!getsockname(listener, (void *)&address, &size));
    pthread_t thread; assert(!pthread_create(&thread, NULL, server, NULL));
    SstConfig config = {.host = "127.0.0.1", .port = ntohs(address.sin_port), .name = "Diagnostics"};
    for (int generation = 0; generation < 2; generation++) {
        sst_net_start(&config); wait_phase(1 + generation * 4); usleep(30000);
        SstConnectionStats stats = sst_net_stats();
        assert(stats.online && !stats.has_rtt && !stats.rtt_ms); // short/wrong PONG and old session ignored
        atomic_store(&phase, 2 + generation * 4); wait_phase(3 + generation * 4);
        uint64_t until = sst_now_ms() + 3000;
        do { stats = sst_net_stats(); if (stats.has_rtt) break; usleep(1000); } while (sst_now_ms() < until);
        assert(stats.online && stats.has_rtt && stats.rtt_ms >= 120 && stats.rtt_ms < 2000 && stats.last_packet_age_ms < 250);
        atomic_store(&phase, 4 + generation * 4);
        while (sst_net_stats().online && sst_now_ms() < until) usleep(1000);
        stats = sst_net_stats(); assert(!stats.online && !stats.has_rtt && !stats.rtt_ms && !stats.queued_bytes);
        sst_net_stop();
    }
    pthread_join(thread, NULL); close(listener);
    puts("network diagnostics: matched delayed PONG, malformed tokens, disconnect and new-session reset PASS");
}

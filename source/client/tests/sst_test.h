// Minimal test helpers for the session-001 regression tests. Loopback only: no test may
// contact a non-local address.
#pragma once
#include "../src/sst_socket.h"
#include "../src/sst_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int sst_test_failures, sst_test_checks;
#define CHECK(cond) do { sst_test_checks++; if (!(cond)) { sst_test_failures++; \
    fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define RUN(fn) do { fprintf(stderr, "[ RUN  ] %s\n", #fn); int before = sst_test_failures; \
    uint64_t t0 = sst_now_ms(); fn(); fprintf(stderr, "[ %s ] %s (%llu ms)\n", \
    sst_test_failures == before ? " OK " : "FAIL", #fn, (unsigned long long)(sst_now_ms() - t0)); } while (0)
#define TEST_EXIT() do { fprintf(stderr, "%d checks, %d failures\n", sst_test_checks, sst_test_failures); \
    return sst_test_failures ? 1 : 0; } while (0)

static inline void sleep_ms(unsigned ms) { usleep(ms * 1000u); }

// Polls `cond` every 10 ms until true or `ms` elapses.
#define WAIT_UNTIL(cond, ms) ({ uint64_t _end = sst_now_ms() + (ms); bool _ok; \
    while (!(_ok = (cond)) && sst_now_ms() < _end) sleep_ms(10); _ok; })

// ---- raw protocol peer (speaks the framing directly, like a second game on another PC)
typedef struct { SstSocket fd; uint8_t rx[70000]; size_t n; } RawPeer;

static inline bool raw_connect(RawPeer *p, uint16_t port) {
    memset(p, 0, sizeof *p);
    sst_socket_init();
    p->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (p->fd == SST_INVALID_SOCKET) return false;
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(p->fd, (void *)&a, sizeof a) != 0) { sst_socket_close(p->fd); p->fd = SST_INVALID_SOCKET; return false; }
    return true;
}
static inline void raw_close(RawPeer *p) {
    if (p->fd != SST_INVALID_SOCKET) sst_socket_close(p->fd);
    p->fd = SST_INVALID_SOCKET;
}
static inline bool raw_send(RawPeer *p, uint8_t type, const uint8_t *body, size_t n) {
    uint8_t b[600]; uint32_t len = (uint32_t)n + 1;
    memcpy(b, &len, 4); b[4] = type; memcpy(b + 5, body, n);
    return send(p->fd, (const char *)b, (int)(n + 5), 0) == (int)(n + 5);
}
static inline size_t put_str(uint8_t *b, const char *s) {
    size_t n = strlen(s); b[0] = (uint8_t)n; memcpy(b + 1, s, n); return n + 1;
}
static inline bool raw_hello(RawPeer *p, uint16_t proto, const char *name, const char *room, const char *team, uint8_t session_byte) {
    uint8_t b[300]; size_t n = 0;
    b[n++] = proto & 0xff; b[n++] = proto >> 8;
    n += put_str(b + n, name); n += put_str(b + n, room); n += put_str(b + n, team);
    memset(b + n, session_byte, 16); n += 16;
    return raw_send(p, 0x01, b, n);
}
// Reads one frame within `ms`. Returns its type, or -1 on timeout/close.
static inline int raw_read(RawPeer *p, uint8_t *body, size_t cap, size_t *len_out, int ms) {
    uint64_t end = sst_now_ms() + (uint64_t)ms;
    for (;;) {
        if (p->n >= 4) {
            uint32_t len; memcpy(&len, p->rx, 4);
            if (len >= 1 && p->n >= 4 + len) {
                int type = p->rx[4]; size_t bn = len - 1;
                if (body) memcpy(body, p->rx + 5, bn < cap ? bn : cap);
                if (len_out) *len_out = bn;
                memmove(p->rx, p->rx + 4 + len, p->n - 4 - len); p->n -= 4 + len;
                return type;
            }
        }
        int left = (int)(end - sst_now_ms());
        if ((int64_t)(end - sst_now_ms()) <= 0) return -1;
        SstPoll pf = {p->fd, POLLIN, 0};
        if (sst_socket_poll(&pf, 1, left) <= 0) return -1;
        int got = recv(p->fd, (char *)p->rx + p->n, (int)(sizeof p->rx - p->n), 0);
        if (got <= 0) return -1;
        p->n += (size_t)got;
    }
}
// Reads frames until one of `type` arrives.
static inline int raw_expect(RawPeer *p, int type, uint8_t *body, size_t cap, size_t *len, int ms) {
    uint64_t end = sst_now_ms() + (uint64_t)ms;
    while (sst_now_ms() < end) {
        int t = raw_read(p, body, cap, len, (int)(end - sst_now_ms()));
        if (t < 0) return -1;
        if (t == type) return t;
    }
    return -1;
}

// A loopback listener for fake hosts. Returns the bound port.
static inline SstSocket listen_loopback(uint16_t *port) {
    sst_socket_init();
    SstSocket s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, (void *)&a, sizeof a) || listen(s, 4)) { sst_socket_close(s); return SST_INVALID_SOCKET; }
    SstSocklen len = sizeof a; getsockname(s, (void *)&a, &len);
    *port = ntohs(a.sin_port);
    return s;
}
// Accepts within `ms` (blocking socket with poll).
static inline SstSocket accept_within(SstSocket listener, int ms) {
    SstPoll pf = {listener, POLLIN, 0};
    if (sst_socket_poll(&pf, 1, ms) != 1) return SST_INVALID_SOCKET;
    return accept(listener, NULL, NULL);
}

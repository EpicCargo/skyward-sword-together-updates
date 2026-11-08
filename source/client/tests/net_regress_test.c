// Session-001 networking regressions: handshake, timeout, reconnect, protocol mismatch,
// disconnect cleanup and save independence of the handshake. Loopback only.
#include "sst_test.h"
#include "../src/sst_host.h"

static bool online(void) { return sst_net_status(NULL, 0) == SST_ONLINE; }

static SstConfig config(uint16_t port, bool reconnect) {
    SstConfig c = {.port = port, .auto_reconnect = reconnect, .use_udp = false};
    snprintf(c.host, sizeof c.host, "127.0.0.1");
    snprintf(c.name, sizeof c.name, "tester");
    snprintf(c.room, sizeof c.room, "room");
    return c;
}

// 1. Successful local connection and protocol handshake against the embedded host.
static void handshake_succeeds(void) {
    char why[160];
    CHECK(sst_host_start(0, "room", why, sizeof why));
    uint16_t port = sst_host_port();
    CHECK(port != 0);
    uint32_t generation = sst_net_generation();
    SstConfig c = config(port, false);
    sst_net_start(&c);
    CHECK(WAIT_UNTIL(online(), 3000));
    CHECK(sst_net_my_id() != 0);
    CHECK(sst_net_generation() == generation + 1);
    // A second game on another PC joins through the same listener.
    RawPeer peer;
    CHECK(raw_connect(&peer, port));
    CHECK(raw_hello(&peer, SST_PROTO, "friend", "room", "", 7));
    uint8_t body[256]; size_t n = 0;
    CHECK(raw_expect(&peer, 0x02, body, sizeof body, &n, 2000) == 0x02);
    SstPlayer players[4];
    CHECK(WAIT_UNTIL(sst_net_players(players, 4) == 1, 2000));
    raw_close(&peer);
    sst_net_stop();
    sst_host_stop();
}

// 2. Unreachable host: no SYN-ACK and no RST. A conditional-accept listener that never
// accepts reproduces a silently dropping firewall entirely on loopback.
static void unreachable_host_times_out(void) {
#ifdef _WIN32
    sst_socket_init();
    SstSocket s = socket(AF_INET, SOCK_STREAM, 0);
    BOOL on = TRUE;
    CHECK(!setsockopt(s, SOL_SOCKET, SO_CONDITIONAL_ACCEPT, (const char *)&on, sizeof on));
    struct sockaddr_in a = {.sin_family = AF_INET};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(!bind(s, (void *)&a, sizeof a) && !listen(s, 1));
    SstSocklen len = sizeof a; getsockname(s, (void *)&a, &len);
    uint16_t port = ntohs(a.sin_port);
    SstConfig c = config(port, false);
    uint64_t t0 = sst_now_ms();
    sst_net_start(&c);
    char why[160] = "";
    CHECK(WAIT_UNTIL(sst_net_status(why, sizeof why) == SST_OFFLINE && why[0], 9000));
    uint64_t took = sst_now_ms() - t0;
    char expected[64]; snprintf(expected, sizeof expected, "connect 127.0.0.1:%u: timed out", port);
    fprintf(stderr, "  reported after %llu ms: %s\n", (unsigned long long)took, why);
    CHECK(!strncmp(why, expected, strlen(expected)));
    CHECK(took >= 4500 && took < 8000);
    sst_net_stop();
    sst_socket_close(s);
#else
    fprintf(stderr, "  skipped: needs SO_CONDITIONAL_ACCEPT (Windows)\n");
#endif
}

// A port with no listener is refused (RST), which must not be reported as a timeout.
static void closed_port_is_refused(void) {
    uint16_t port;
    SstSocket s = listen_loopback(&port);
    sst_socket_close(s); // nothing listens there now
    SstConfig c = config(port, false);
    sst_net_start(&c);
    char why[160] = "";
    CHECK(WAIT_UNTIL(sst_net_status(why, sizeof why) == SST_OFFLINE && why[0], 9000));
    fprintf(stderr, "  reported: %s\n", why);
    CHECK(strstr(why, "timed out") == NULL);
    sst_net_stop();
}

// 3. Reconnection after the host restarts on the same port.
static void reconnects_after_host_restart(void) {
    char why[160];
    CHECK(sst_host_start(0, "room", why, sizeof why));
    uint16_t port = sst_host_port();
    SstConfig c = config(port, true);
    sst_net_start(&c);
    CHECK(WAIT_UNTIL(online(), 3000));
    uint32_t generation = sst_net_generation();
    sst_host_stop();
    CHECK(WAIT_UNTIL(!online(), 3000));
    bool restarted = sst_host_start(port, "room", why, sizeof why);
    if (!restarted) fprintf(stderr, "  host restart on port %u failed: %s\n", port, why);
    CHECK(restarted);
    CHECK(WAIT_UNTIL(online(), 8000));
    CHECK(sst_net_generation() > generation);
    sst_net_stop();
    sst_host_stop();
}

// 4. Protocol version mismatch: the host refuses, and the client stops instead of retrying.
static void protocol_mismatch_is_rejected(void) {
    char why[160];
    CHECK(sst_host_start(0, "room", why, sizeof why));
    RawPeer old;
    CHECK(raw_connect(&old, sst_host_port()));
    CHECK(raw_hello(&old, SST_PROTO - 1, "old", "room", "", 1));
    uint8_t body[256] = {0}; size_t n = 0;
    CHECK(raw_expect(&old, 0x05, body, sizeof body - 1, &n, 2000) == 0x05);
    CHECK(n > 1 && !memcmp(body + 1, "Update the mod: protocol mismatch", body[0]));
    raw_close(&old);
    sst_host_stop();

    // Client side: a host from another build answers ERROR. auto_reconnect must not loop.
    uint16_t port; SstSocket l = listen_loopback(&port);
    SstConfig c = config(port, true);
    sst_net_start(&c);
    SstSocket fd = accept_within(l, 3000);
    CHECK(fd != SST_INVALID_SOCKET);
    RawPeer host = {.fd = fd};
    CHECK(raw_read(&host, body, sizeof body, &n, 2000) == 0x01);
    CHECK(n >= 2 && (body[0] | body[1] << 8) == SST_PROTO);
    uint8_t err[64]; size_t e = put_str(err, "Update the mod: protocol mismatch");
    CHECK(raw_send(&host, 0x05, err, e));
    char text[160] = "";
    CHECK(WAIT_UNTIL(sst_net_status(text, sizeof text) == SST_FAILED, 3000));
    CHECK(strstr(text, "protocol mismatch") != NULL);
    CHECK(accept_within(l, 2500) == SST_INVALID_SOCKET); // no reconnect attempt
    sst_net_stop();
    raw_close(&host);
    sst_socket_close(l);
}

// 5. Disconnect cleanup: departed peers vanish and the host frees their slot.
static void disconnect_cleans_up(void) {
    char why[160];
    CHECK(sst_host_start(0, "room", why, sizeof why));
    uint16_t port = sst_host_port();
    SstConfig c = config(port, false);
    sst_net_start(&c);
    CHECK(WAIT_UNTIL(online(), 3000));
    RawPeer peers[3];
    for (int i = 0; i < 3; i++) {
        CHECK(raw_connect(&peers[i], port));
        CHECK(raw_hello(&peers[i], SST_PROTO, "p", "room", "", (uint8_t)(10 + i)));
        CHECK(raw_expect(&peers[i], 0x02, NULL, 0, NULL, 2000) == 0x02);
    }
    SstPlayer list[8];
    CHECK(WAIT_UNTIL(sst_net_players(list, 8) == 3, 2000));
    // Session is full (4): a fifth player is refused.
    RawPeer extra;
    CHECK(raw_connect(&extra, port));
    CHECK(raw_hello(&extra, SST_PROTO, "x", "room", "", 99));
    CHECK(raw_expect(&extra, 0x05, NULL, 0, NULL, 2000) == 0x05);
    raw_close(&extra);
    raw_close(&peers[0]);
    CHECK(WAIT_UNTIL(sst_net_players(list, 8) == 2, 2000));
    // The freed slot is reusable.
    CHECK(raw_connect(&extra, port));
    CHECK(raw_hello(&extra, SST_PROTO, "late", "room", "", 42));
    CHECK(raw_expect(&extra, 0x02, NULL, 0, NULL, 2000) == 0x02);
    CHECK(WAIT_UNTIL(sst_net_players(list, 8) == 3, 2000));
    // Local stop clears the remote list.
    sst_net_stop();
    CHECK(sst_net_players(list, 8) == 0);
    CHECK(sst_net_status(NULL, 0) == SST_OFFLINE);
    raw_close(&extra); raw_close(&peers[1]); raw_close(&peers[2]);
    sst_host_stop();
}

// 6. Save-state independence: HELLO carries only protocol, name, room, team and a random
// session. No save, progression or scene data can affect whether the TCP connect succeeds.
static void hello_contains_no_save_state(void) {
    uint16_t port; SstSocket l = listen_loopback(&port);
    SstConfig c = config(port, false);
    sst_net_start(&c);
    SstScene scene = {.stage = "F000", .room = 3};
    sst_net_send_scene(&scene);               // a "progressed" player before connecting
    sst_net_send_flag(0, 123, 1);             // pending shared progress
    SstSocket fd = accept_within(l, 3000);
    CHECK(fd != SST_INVALID_SOCKET);
    RawPeer host = {.fd = fd};
    uint8_t body[300]; size_t n = 0;
    CHECK(raw_read(&host, body, sizeof body, &n, 2000) == 0x01);
    size_t expect = 2 + (1 + strlen("tester")) + (1 + strlen("room")) + 1 + 16;
    CHECK(n == expect);
    CHECK((body[0] | body[1] << 8) == SST_PROTO);
    // Before WELCOME only a 4-byte keepalive PING may follow (no scene, flags or progress).
    int t;
    while ((t = raw_read(&host, body, sizeof body, &n, 500)) != -1) CHECK(t == 0x06 && n == 4);
    sst_net_stop();
    raw_close(&host);
    sst_socket_close(l);
}

int main(void) {
    setvbuf(stderr, NULL, _IONBF, 0);
#ifdef _WIN32
    _putenv("SST_HOST_BIND=127.0.0.1");
#else
    setenv("SST_HOST_BIND","127.0.0.1",1);
#endif
    RUN(handshake_succeeds);
    RUN(unreachable_host_times_out);
    RUN(closed_port_is_refused);
    RUN(reconnects_after_host_restart);
    RUN(protocol_mismatch_is_rejected);
    RUN(disconnect_cleans_up);
    RUN(hello_contains_no_save_state);
    TEST_EXIT();
}
